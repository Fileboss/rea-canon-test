# Extraction et réécriture pour Apple Silicon d'un pilote pour imprimante Canon MX870
Le but de ce repo est de reverse le pilote de l'imprimante Canon MX870 afin d'en écrire une version moderne et fonctionnelle sur apple silicon, en utilisant REA.

## Pourquoi ?
- Il n'existe pas de pilote officiel de la part de canon pour cette imprimante compatible Apple Silicon 64 bits, seulement Windows x86 (32bits)
- Les applications et serveurs multi imprimantes compatibles n'offrents pas un accès exhaustif aux fonctionalités de l'imprimante
- Gutenprint est déprécié sur HomeBrew, et la compatibilité n'était pas complète (mode inversé lors des impressions en couleur)
- Pédagogique : cela me permet de mettre le pieds dans le reverse

## Mode Opératoire
- Décompresser l'exécutable Winwdows présente dans ce dépot
- à l'aide de REA (Reverse Engineer Anything) installé sur cette machine via NPX, analyser l'exécutable et extraier le protocole de l'imprimante
- Ecrire une version compatible macos 27, M5 offrant un accès à toutes les fonctionalité de l'imprimante, natiement

## Notes
- Usage personnel uniquement
- Le binaire original ne doit pas êttre publié dans le repo