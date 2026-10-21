
#ifndef CHATBOT_H
#define CHATBOT_H
#include "structures.h"

int  chatbot_repondre(const Mesure *tab, int n, const char *question, char *rep, size_t taille);


void chatbot_menu(const Mesure *tab, int n);

#endif
