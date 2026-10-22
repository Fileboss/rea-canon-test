# Extraction et réécriture pour Apple Silicon d'un pilote pour imprimante Canon MX870
Le but de ce dépôt est de rétro-concevoir le pilote de l'imprimante Canon MX870 afin d'en écrire une version moderne et fonctionnelle sur Apple Silicon, en utilisant REA.

## Pourquoi ?
- Il n'existe pas de pilote officiel de Canon pour cette imprimante compatible Apple Silicon 64 bits, seulement Windows x86 (32 bits)
- Les applications et serveurs multi-imprimantes compatibles n'offrent pas un accès exhaustif aux fonctionnalités de l'imprimante
- Gutenprint est déprécié sur Homebrew, et la compatibilité n'était pas complète (mode inversé lors des impressions en couleur)
- Pédagogique : cela me permet de mettre le pied dans la rétro-ingénierie

## Mode opératoire
- Décompresser l'exécutable Windows, fourni localement (non versionné)
- À l'aide de REA (Reverse Engineer Anything), installé sur cette machine via npx, analyser l'exécutable et en extraire le protocole de l'imprimante
- Écrire une version compatible macOS 27 / M5, offrant nativement un accès à toutes les fonctionnalités de l'imprimante

## Notes
- Usage personnel uniquement
- Le binaire original ne doit pas être publié dans le dépôt
