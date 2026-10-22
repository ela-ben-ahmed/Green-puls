# EcoCampus – Module C (traitement + chatbot IA)

## Fichiers
| Fichier |  Rôle |
|---|---|
| structures.h, gestion.h  | types et prototypes |
| gestion.c  | chargement CSV, ajout, recherche, suppression, affichage |
| analyse.c | tri, statistiques, anomalies, export JSON |
| chatbot.h / chatbot.c | assistant IA (API Groq) qui répond sur les données du campus |
| main.c | menu interactif (option 11 = chatbot) et mode automatique |
| measures_in.csv / measures_erreurs.csv  | jeux de données |

## Compilation (GCC, C99)
    gcc -std=c99 -Wall -Wextra -o ecocampus main.c gestion.c analyse.c chatbot.c

## Exécution
    ./ecocampus measures_in.csv data.json   # mode automatique -> génère data.json
    ./ecocampus                              # menu (11 = Chatbot IA)

## Chatbot IA
- Utilise l'API Groq (modèle openai/gpt-oss-120b, comme le projet Python) via la commande `curl`
  (déjà présente sur Windows 10/11, Linux, macOS : aucune bibliothèque à installer).
- Clé : créer un fichier `.env` à côté de l'exécutable (ou dans le dossier parent) :
      GROQ_API_KEY=votre_cle
  ou définir la variable d'environnement GROQ_API_KEY.
- Le chatbot envoie à l'IA les mesures et statistiques chargées : il répond aux questions sur les bâtiments et les alertes.
- Sans clé ou sans Internet : réponses locales de secours (par bâtiment, ou "alertes").
- Commandes : `/reset` nouvelle conversation, `/quit` retour au menu.
- Variables optionnelles : ECOCAMPUS_MODEL, ECOCAMPUS_API_URL.
- SÉCURITÉ : `.env` est dans `.gitignore` ; ne jamais le publier sur GitHub.
