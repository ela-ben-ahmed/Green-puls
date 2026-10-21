/* chatbot.h - Assistant IA EcoCampus (Module C) */
#ifndef CHATBOT_H
#define CHATBOT_H
#include "structures.h"

/* Pose une question a l'IA (API Claude via curl) en lui donnant les donnees du campus.
   Ecrit la reponse dans rep. Retourne 1 si la reponse vient de l'IA,
   0 si l'IA est indisponible (reponse locale de secours). */
int  chatbot_repondre(const Mesure *tab, int n, const char *question, char *rep, size_t taille);

/* Boucle de conversation en console (/reset, /quit). */
void chatbot_menu(const Mesure *tab, int n);

#endif
