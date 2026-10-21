

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "chatbot.h"
#include "gestion.h"

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#else
#define POPEN  popen
#define PCLOSE pclose
#endif

#define FIC_REQ "ecocampus_req.tmp"
#define FIC_CFG "ecocampus_cfg.tmp"
#define MAX_HIST 12                      


typedef struct { char *s; size_t len, cap; } Buf;

static int buf_add(Buf *b, const char *t) {
    size_t l = strlen(t);
    if (b->len + l + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        while (nc < b->len + l + 1) nc *= 2;
        char *p = realloc(b->s, nc);
        if (!p) return 0;
        b->s = p; b->cap = nc;
    }
    memcpy(b->s + b->len, t, l + 1);
    b->len += l;
    return 1;
}

static int buf_add_json(Buf *b, const char *t) {
    char tmp[8];
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        if (c == '"')       { if (!buf_add(b, "\\\"")) return 0; }
        else if (c == '\\') { if (!buf_add(b, "\\\\")) return 0; }
        else if (c == '\n') { if (!buf_add(b, "\\n"))  return 0; }
        else if (c == '\r') { if (!buf_add(b, "\\r"))  return 0; }
        else if (c == '\t') { if (!buf_add(b, "\\t"))  return 0; }
        else if (c < 0x20)  { snprintf(tmp, sizeof tmp, "\\u%04x", c); if (!buf_add(b, tmp)) return 0; }
        else { tmp[0] = (char)c; tmp[1] = '\0'; if (!buf_add(b, tmp)) return 0; }
    }
    return 1;
}


static struct { char role[10]; char *texte; } hist[MAX_HIST];
static int nb_hist = 0;

static void hist_vider(void) {
    for (int i = 0; i < nb_hist; i++) free(hist[i].texte);
    nb_hist = 0;
}
static void hist_ajouter(const char *role, const char *texte) {
    if (nb_hist == MAX_HIST) {                       /* on retire le plus ancien echange */
        free(hist[0].texte); free(hist[1].texte);
        memmove(&hist[0], &hist[2], (MAX_HIST - 2) * sizeof hist[0]);
        nb_hist -= 2;
    }
    strcpy(hist[nb_hist].role, role);
    hist[nb_hist].texte = malloc(strlen(texte) + 1);
    if (hist[nb_hist].texte) { strcpy(hist[nb_hist].texte, texte); nb_hist++; }
}
static void hist_retirer_dernier(void) {
    if (nb_hist > 0) { free(hist[--nb_hist].texte); }
}


static int construire_contexte(Buf *b, const Mesure *tab, int n) {
    char l[256];
    if (!buf_add(b, "Tu es l'assistant du projet EcoCampus, un outil de suivi de la consommation "
                    "energetique (electricite kWh, eau m3, chauffage kWh) des batiments d'un campus. "
                    "Reponds en francais, de facon courte et claire, uniquement a partir des donnees "
                    "ci-dessous. Une mesure est en alerte quand valeur > seuil. "
                    "Si une information n'est pas dans les donnees, dis-le.\n\n"
                    "MESURES (id;batiment;ressource;valeur;seuil;date;heure;alerte) :\n")) return 0;
    int max = n < 300 ? n : 300;
    for (int i = 0; i < max; i++) {
        snprintf(l, sizeof l, "%d;%s;%s;%.1f;%.1f;%s;%s;%s\n", tab[i].id, tab[i].batiment,
                 tab[i].ressource, tab[i].valeur, tab[i].seuil, tab[i].date, tab[i].heure,
                 tab[i].alerte ? "oui" : "non");
        if (!buf_add(b, l)) return 0;
    }
    Stat st[64]; int ns = calculer_statistiques(tab, n, st, 64);
    if (!buf_add(b, "\nSTATISTIQUES PAR BATIMENT :\n")) return 0;
    for (int k = 0; k < ns; k++) {
        snprintf(l, sizeof l, "%s : %d mesures, total %.1f, moyenne %.1f, min %.1f, max %.1f, %d alerte(s)\n",
                 st[k].batiment, st[k].nb, st[k].total, st[k].moyenne, st[k].min, st[k].max, st[k].nb_alertes);
        if (!buf_add(b, l)) return 0;
    }
    return 1;
}


static const char *lire_cle(char *buf, size_t taille) {
    const char *e = getenv("GROQ_API_KEY");
    if (e && *e) return e;
    const char *chemins[] = { ".env", "../.env" };
    for (int c = 0; c < 2; c++) {
        FILE *f = fopen(chemins[c], "r");
        if (!f) continue;
        char l[512];
        while (fgets(l, sizeof l, f)) {
            l[strcspn(l, "\r\n")] = '\0';
            if (strncmp(l, "GROQ_API_KEY=", 13) == 0) {
                const char *v = l + 13;
                if (*v == '"' || *v == '\'') v++;
                snprintf(buf, taille, "%s", v);
                size_t k = strlen(buf);
                while (k > 0 && (buf[k-1] == '"' || buf[k-1] == '\'' || buf[k-1] == ' ')) buf[--k] = '\0';
                fclose(f);
                return buf[0] ? buf : NULL;
            }
        }
        fclose(f);
    }
    return NULL;
}


static int extraire_texte(const char *json, const char *cle, char *out, size_t taille) {
    char motif[40]; snprintf(motif, sizeof motif, "\"%s\"", cle);
    const char *p = strstr(json, motif);
    while (p) {
        const char *q = p + strlen(motif);
        while (*q == ' ' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\n') q++;
            if (*q == '"') { p = q + 1; break; }
        }
        p = strstr(p + 1, motif);
    }
    if (!p) return 0;
    size_t o = 0;
    while (*p && *p != '"' && o + 4 < taille) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': break;
                case 'u': {
                    unsigned cp = 0;
                    if (sscanf(p + 1, "%4x", &cp) == 1) {
                        p += 4;
                        if (cp < 0x80) out[o++] = (char)cp;
                        else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                        else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                    }
                    break; }
                default: out[o++] = *p;          /* \" \\ \/ */
            }
            p++;
        } else out[o++] = *p++;
    }
    out[o] = '\0';
    return 1;
}


static int appeler_ia(const Mesure *tab, int n, char *rep, size_t taille) {
    char cle_buf[256];
    const char *cle = lire_cle(cle_buf, sizeof cle_buf);
    if (!cle) { snprintf(rep, taille, "cle API absente (GROQ_API_KEY dans l'environnement ou dans .env)"); return 0; }
    const char *modele = getenv("ECOCAMPUS_MODEL"); if (!modele || !*modele) modele = "openai/gpt-oss-120b";
    const char *url = getenv("ECOCAMPUS_API_URL");  if (!url || !*url) url = "https://api.groq.com/openai/v1/chat/completions";

    Buf ctx = {0}, req = {0};
    if (!construire_contexte(&ctx, tab, n)) { snprintf(rep, taille, "memoire insuffisante"); free(ctx.s); return 0; }
    int ok = buf_add(&req, "{\"model\":\"") && buf_add_json(&req, modele) &&
             buf_add(&req, "\",\"temperature\":0.7,\"max_completion_tokens\":1500,\"messages\":["
                           "{\"role\":\"system\",\"content\":\"") && buf_add_json(&req, ctx.s) && buf_add(&req, "\"}");
    for (int i = 0; ok && i < nb_hist; i++) {
        ok = buf_add(&req, ",{\"role\":\"") && buf_add(&req, hist[i].role) &&
             buf_add(&req, "\",\"content\":\"") && buf_add_json(&req, hist[i].texte) && buf_add(&req, "\"}");
    }
    ok = ok && buf_add(&req, "]}");
    free(ctx.s);
    if (!ok) { snprintf(rep, taille, "memoire insuffisante"); free(req.s); return 0; }

    FILE *f = fopen(FIC_REQ, "w");
    if (!f) { snprintf(rep, taille, "impossible d'ecrire %s", FIC_REQ); free(req.s); return 0; }
    fputs(req.s, f); fclose(f); free(req.s);

    f = fopen(FIC_CFG, "w");                          /* la cle reste hors de la ligne de commande */
    if (!f) { snprintf(rep, taille, "impossible d'ecrire %s", FIC_CFG); remove(FIC_REQ); return 0; }
    fprintf(f, "url = \"%s\"\nrequest = \"POST\"\nsilent\nmax-time = 90\n"
               "header = \"content-type: application/json\"\n"
               "header = \"authorization: Bearer %s\"\n"
               "data-binary = \"@%s\"\n", url, cle, FIC_REQ);
    fclose(f);

    FILE *p = POPEN("curl -K " FIC_CFG, "r");
    if (!p) { snprintf(rep, taille, "commande curl introuvable"); remove(FIC_REQ); remove(FIC_CFG); return 0; }
    Buf rb = {0}; char tmp[1024]; size_t r;
    while ((r = fread(tmp, 1, sizeof tmp - 1, p)) > 0) { tmp[r] = '\0'; if (!buf_add(&rb, tmp)) break; }
    PCLOSE(p);
    remove(FIC_REQ); remove(FIC_CFG);                 /* ne laisse ni cle ni requete sur le disque */

    int res = 0;
    if (!rb.s || !*rb.s) snprintf(rep, taille, "pas de reponse (connexion Internet ?)");
    else if (!strstr(rb.s, "\"choices\"")) {
        char msg[300];
        if (!extraire_texte(rb.s, "message", msg, sizeof msg)) strcpy(msg, "reponse inattendue");
        snprintf(rep, taille, "erreur API : %s", msg);
    }
    else if (extraire_texte(rb.s, "content", rep, taille) && rep[0]) res = 1;
    else snprintf(rep, taille, "reponse vide ou illisible");
    free(rb.s);
    return res;
}


static int contient(const char *txt, const char *mot) {
    size_t lm = strlen(mot);
    for (; *txt; txt++) {
        size_t i = 0;
        while (i < lm && txt[i] && tolower((unsigned char)txt[i]) == tolower((unsigned char)mot[i])) i++;
        if (i == lm) return 1;
    }
    return 0;
}
static void reponse_locale(const Mesure *tab, int n, const char *q, char *rep, size_t taille) {
    size_t o = 0; rep[0] = '\0';
    Stat st[64]; int ns = calculer_statistiques(tab, n, st, 64);
    int trouve = 0;
    for (int k = 0; k < ns && o + 200 < taille; k++)
        if (contient(q, st[k].batiment)) {
            o += snprintf(rep + o, taille - o, "%s : %d mesures, total %.1f, moyenne %.1f, min %.1f, max %.1f, %d alerte(s).\n",
                          st[k].batiment, st[k].nb, st[k].total, st[k].moyenne, st[k].min, st[k].max, st[k].nb_alertes);
            trouve = 1;
        }
    if (contient(q, "alerte") || contient(q, "anomalie") || contient(q, "depass")) {
        o += snprintf(rep + o, taille - o, "Mesures en alerte :\n");
        for (int i = 0; i < n && o + 150 < taille; i++)
            if (tab[i].alerte)
                o += snprintf(rep + o, taille - o, " - %s / %s : %.1f > %.1f (%s %s)\n", tab[i].batiment,
                              tab[i].ressource, tab[i].valeur, tab[i].seuil, tab[i].date, tab[i].heure);
        trouve = 1;
    }
    if (!trouve)
        snprintf(rep, taille, "Mode local : je sais repondre sur un batiment (ex. \"Residence\") ou sur les "
                              "\"alertes\". Pour des questions libres, configurez GROQ_API_KEY.");
}

/* ---------- interface ---------- */
int chatbot_repondre(const Mesure *tab, int n, const char *question, char *rep, size_t taille) {
    hist_ajouter("user", question);
    if (appeler_ia(tab, n, rep, taille)) { hist_ajouter("assistant", rep); return 1; }
    char raison[300]; snprintf(raison, sizeof raison, "%s", rep);
    hist_retirer_dernier();                           /* la question n'a pas recu de reponse IA */
    reponse_locale(tab, n, question, rep, taille);
    size_t l = strlen(rep);
    snprintf(rep + l, taille - l, "\n[IA indisponible : %s]", raison);
    return 0;
}

void chatbot_menu(const Mesure *tab, int n) {
    char q[512], rep[6000];
    printf("\n=== Chatbot EcoCampus (IA) ===\n"
           "Posez vos questions sur la consommation. /reset = nouvelle conversation, /quit = retour.\n");
    { char kb[256]; if (!lire_cle(kb, sizeof kb)) printf("(Cle API absente : mode local limite.)\n"); }
    hist_vider();
    for (;;) {
        printf("\nVous > ");
        if (!fgets(q, sizeof q, stdin)) break;
        q[strcspn(q, "\r\n")] = '\0';
        if (q[0] == '\0') continue;
        if (!strcmp(q, "/quit") || !strcmp(q, "quit") || !strcmp(q, "exit")) break;
        if (!strcmp(q, "/reset")) { hist_vider(); printf("Conversation reinitialisee.\n"); continue; }
        chatbot_repondre(tab, n, q, rep, sizeof rep);
        printf("\nBot > %s\n", rep);
    }
    hist_vider();
}
