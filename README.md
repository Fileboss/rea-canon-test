# Pilote Canon PIXMA MX870 pour macOS (Apple Silicon)

Synthèse illustrée pour non-initiés : [docs/rea-claude-mx870.html](docs/rea-claude-mx870.html).

Pilote natif arm64 pour la MX870, écrit de zéro à partir de la rétro-ingénierie du pilote
Windows 1.06 (voir [docs/protocole-mx870.md](docs/protocole-mx870.md)).

> **Projet indépendant, non affilié à Canon Inc.** Canon et PIXMA sont des marques de
> Canon Inc., citées uniquement pour désigner le matériel compatible.

> **État : non testé sur l'imprimante réelle.** Les flux sont validés par un décodeur
> indépendant (`tools/bjdump.py`) et un simulateur BJNP (`tests/fake_bjnp.py`).
> Les points à vérifier en premier sont listés au §5 du document de protocole.

## Apportez votre propre pilote Canon

Ce dépôt ne contient **aucun fichier Canon** ni aucune des tables du pilote. Seuls quelques
faits nécessaires à l'interopérabilité y figurent : codes de format, de support et de bac,
dimensions des papiers (dans `src/libmx870/tables.c` et `docs/protocole-mx870.md`), pour la
plupart également publiés par Gutenprint. Le pilote a besoin en plus des réglages
d'impression de l'imprimante (30 modes papier × qualité, marges exactes de 21 formats) ; ils
sont **générés sur votre machine, au moment de la construction**, à partir de votre propre
copie de l'installeur Windows officiel :

1. Téléchargez chez Canon le fichier `mp68-win-mx870-1_06-ea24.exe`
   (*MX870 series MP Driver Ver.1.06 (Windows)*), par exemple sur
   [asia.canon/en/support/0100462001](https://asia.canon/en/support/0100462001).
2. Lancez `./install.sh chemin/vers/mp68-win-mx870-1_06-ea24.exe`.

Le script vérifie l'empreinte SHA-256 du fichier
(`464d5dcd69c374a25085d85316031dadf3758f11e4f903ce42d971fc55cf7e14`), en extrait un seul
fichier (`cnm_0367.dll`, la base de données du modèle) dans `build/assets/`, génère les
tables dans `build/generated/`, compile, teste et installe. Rien de cela n'est versionné.

Pourquoi : la loi autorise l'analyse d'un logiciel pour l'interopérabilité, mais les
informations obtenues ne doivent pas être diffusées au-delà de ce qui est nécessaire
(article L122-6-1 du [Code de la propriété intellectuelle](https://www.legifrance.gouv.fr/codes/id/LEGITEXT000006069414),
issu de la [directive 2009/24/CE](https://eur-lex.europa.eu/eli/dir/2009/24/oj)). Chacun
reconstitue donc les tables depuis le pilote qu'il a lui-même obtenu auprès de Canon.

## Installer

```sh
./install.sh mp68-win-mx870-1_06-ea24.exe                         # pilote CUPS classique
./install.sh --printer 192.168.1.20 mp68-win-mx870-1_06-ea24.exe  # + impression réseau
./install.sh --app --printer 192.168.1.20 mp68-win-mx870-1_06-ea24.exe   # + application AirPrint
./install.sh --no-install mp68-win-mx870-1_06-ea24.exe            # construire et tester seulement
```

Prérequis : macOS sur Apple Silicon, les outils en ligne de commande Xcode
(`xcode-select --install`), Python 3. Pour l'application AirPrint :
[Homebrew](https://brew.sh) et `brew install jpeg-turbo libpng libusb openssl@3 pkgconf`.

Deux livrables partagent le même encodeur (`src/libmx870`) et le même client réseau
(`src/libbjnp`) :

| Livrable | Principe | Réseau |
|---|---|---|
| **Pilote CUPS classique** (`rastertomx870` + PPD) | file d'impression [CUPS](https://openprinting.github.io/cups/) avec toutes les options dans le dialogue d'impression | relais local `socket://127.0.0.1:9870` → BJNP, lancé par [launchd](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html) |
| **Application AirPrint** (`mx870-app`, [PAPPL](https://github.com/michaelrsweet/pappl)) | l'imprimante apparaît comme imprimante [AirPrint](https://support.apple.com/en-us/102742) / [IPP Everywhere](https://www.pwg.org/ipp/everywhere.html), sans pilote | `bjnp://` intégré, USB via PAPPL |

Un backend CUPS `bjnp://` n'est pas installable : `/usr/libexec/cups/backend` se trouve sur
le [volume système scellé](https://support.apple.com/guide/security/signed-system-volume-security-secd698747c9/web)
de macOS, d'où le relais local.

**Premier essai conseillé**, sans CUPS ni PAPPL, en brouillon noir pour économiser l'encre :

```sh
build/mx870-bjnp discover
build/mx870-bjnp send <ip> build/samples/sample_letter_plain_draft_gray.bin
```

## Développement

```sh
make WINEXE=chemin/vers/mp68-win-mx870-1_06-ea24.exe   # une fois : extraction des tables
make            # libmx870, filtre CUPS, PPD, mx870-bjnp
make app        # application PAPPL (compile PAPPL 1.4.9 dans build/pappl)
make check      # tests unitaires + validation des flux
make e2e        # PDF -> chaîne CUPS de macOS -> filtre -> décodeur
make net        # BJNP contre le simulateur (y compris réponses « occupé »)
make app-test   # application PAPPL -> simulateur BJNP (dont un travail IPP sans marges)
```

```
install.sh      installation complète à partir de votre installeur Canon
src/libmx870/   encodeur (commandes, bandes, PackBits, base 3, couleur, tramage)
src/libbjnp/    client BJNP (découverte, identifiant, session, envoi acquitté)
src/cups/       filtre CUPS rastertomx870
src/app/        application PAPPL
src/tools/      mx870-bjnp (discover / id / send / relay)
tools/          extraction des tables, décodeur bjdump.py, aperçu, générateur de PPD
tests/          tests unitaires, e2e CUPS, simulateur BJNP, test de l'application
packaging/      plists launchd
build/          tout ce qui est généré, y compris les fichiers Canon extraits (non versionné)
```

## Technologies utilisées

| Technologie | Rôle dans le projet | Lien |
|---|---|---|
| Claude Code | assistant IA : analyse, écriture du code, tests, documentation | [claude.com/product/claude-code](https://claude.com/product/claude-code) |
| REA (Reverse Engineer Anything) | boîte à outils de rétro-ingénierie pilotée par Claude | [github.com/morluto/rea](https://github.com/morluto/rea) |
| Ghidra | décompilateur utilisé par REA (et en ligne de commande) | [ghidra-sre.org](https://ghidra-sre.org) · [GitHub](https://github.com/NationalSecurityAgency/ghidra) |
| Eclipse Temurin (Java) | environnement Java requis par Ghidra | [adoptium.net](https://adoptium.net) |
| Hopper Disassembler | désassembleur essayé en premier | [hopperapp.com](https://www.hopperapp.com) |
| Gutenprint | pilotes libres Canon, référence pour recouper les déductions | [gimp-print.sourceforge.io](https://gimp-print.sourceforge.io) |
| cups-bjnp | backend BJNP libre, référence pour le protocole réseau | [sourceforge.net/projects/cups-bjnp](https://sourceforge.net/projects/cups-bjnp/) |
| CUPS | système d'impression de macOS | [openprinting.github.io/cups](https://openprinting.github.io/cups/) · [GitHub Apple](https://github.com/apple/cups) |
| PAPPL | bibliothèque d'« applications d'impression » IPP Everywhere | [github.com/michaelrsweet/pappl](https://github.com/michaelrsweet/pappl) · [msweet.org/pappl](https://www.msweet.org/pappl/) |
| AirPrint / IPP Everywhere | impression sans pilote d'Apple / standard du PWG | [support.apple.com/102742](https://support.apple.com/en-us/102742) · [pwg.org](https://www.pwg.org/ipp/everywhere.html) |
| PWG Raster | format d'image transmis par l'application PAPPL | [spécification PWG 5102.4](https://ftp.pwg.org/pub/pwg/candidates/cs-ippraster10-20120420-5102.4.pdf) |
| PackBits | compression des lignes d'impression | [Wikipédia](https://en.wikipedia.org/wiki/PackBits) |
| Tramage (error diffusion) | conversion des nuances en points d'encre | [Wikipédia](https://fr.wikipedia.org/wiki/Tramage_(informatique)) |
| WinZip Self-Extractor | format de l'installeur Canon | [winzip.com](https://www.winzip.com/en/product/self-extractor/) |
| libarchive (bsdtar) | lecture des archives CAB sous macOS | [libarchive.org](https://www.libarchive.org) |
| Homebrew | gestionnaire de paquets pour les dépendances de PAPPL | [brew.sh](https://brew.sh) |
| libjpeg-turbo, libpng, libusb, OpenSSL | dépendances de PAPPL | [libjpeg-turbo.org](https://libjpeg-turbo.org) · [libpng](http://www.libpng.org/pub/png/libpng.html) · [libusb.info](https://libusb.info) · [openssl.org](https://www.openssl.org) |

## Crédits

- [Gutenprint](https://gimp-print.sourceforge.io) : référence pour recouper le sens des
  commandes ; les messages XML IVEC de début et de fin de travail en sont repris. Ce sont
  des chaînes de protocole fixées par l'imprimante, reprises à l'identique parce qu'elle
  les attend ainsi, et non du code de Gutenprint (sous licence GPL) : aucune partie de son
  code n'est incluse dans ce projet.
- [PAPPL](https://github.com/michaelrsweet/pappl) (Michael R Sweet) pour l'application AirPrint.
- Licence : MIT (voir `LICENSE`).
