# Pilote Canon PIXMA MX870 pour macOS (Apple Silicon)

Synthèse illustrée pour non-initiés : [docs/rea-claude-mx870.html](docs/rea-claude-mx870.html).

Pilote natif arm64 pour la MX870, écrit à partir de la rétro-ingénierie du pilote
Windows 1.06 (voir [docs/protocole-mx870.md](docs/protocole-mx870.md)). Usage personnel.

> **État : non testé sur l'imprimante réelle.** Les flux sont validés par un décodeur
> indépendant (`tools/bjdump.py`) et un simulateur BJNP (`tests/fake_bjnp.py`).
> Les points à vérifier en premier sont listés au §5 du document de protocole.

Deux livrables partagent le même encodeur (`src/libmx870`) et le même client réseau
(`src/libbjnp`) :

| Livrable | Principe | Réseau |
|---|---|---|
| **Pilote CUPS classique** (`rastertomx870` + PPD) | file CUPS avec PPD, toutes options dans le dialogue d'impression | relais local `socket://127.0.0.1:9870` → BJNP (`mx870-bjnp relay`) |
| **Application AirPrint** (`mx870-app`, PAPPL) | l'imprimante apparaît comme imprimante AirPrint sans pilote | `bjnp://` intégré, USB via PAPPL |

## Construire

```sh
make            # libmx870, filtre CUPS, PPD, mx870-bjnp
make app        # application PAPPL (PAPPL 1.4.9 compilé dans vendor/pappl-install)
make check      # tests unitaires + validation des flux
make e2e        # PDF -> chaîne CUPS de macOS -> filtre -> décodeur
make net        # BJNP contre le simulateur (y compris réponses « occupé »)
make app-test   # application PAPPL -> simulateur BJNP (dont un travail IPP sans marges)
```

PAPPL n'a pas de formule Homebrew : il est compilé depuis `michaelrsweet/pappl` v1.4.9
(`./configure --disable-shared --with-tls=openssl`, `-arch arm64`), avec `jpeg-turbo`,
`libpng`, `libusb` et `openssl@3` de Homebrew.

## Installer et imprimer

Trouver l'imprimante sur le réseau : `build/mx870-bjnp discover` (ou `id <ip>`).

**Application AirPrint (recommandé)**

```sh
sudo make install
build/mx870-app server -o server-port=8870 &          # ou packaging/org.mx870.app.plist
build/mx870-app add -d MX870 -v bjnp://<ip> -m canon_mx870
```

L'imprimante « MX870 » apparaît ensuite dans Réglages Système › Imprimantes comme
imprimante AirPrint ; interface web : http://localhost:8870.

**Pilote CUPS classique**

```sh
sudo make install                                      # filtre + PPD + mx870-bjnp
# relais BJNP : remplacer PRINTER_IP dans packaging/org.mx870.bjnp-relay.plist, puis
sudo cp packaging/org.mx870.bjnp-relay.plist /Library/LaunchDaemons/
sudo launchctl bootstrap system /Library/LaunchDaemons/org.mx870.bjnp-relay.plist
lpadmin -p MX870 -E -v socket://127.0.0.1:9870 \
  -P "/Library/Printers/PPDs/Contents/Resources/Canon MX870 libmx870.ppd.gz"
```

Un backend CUPS `bjnp://` n'est pas installable : `/usr/libexec/cups/backend` est sur le
volume système scellé de macOS, d'où le relais local.

**Premier essai conseillé** (sans CUPS ni PAPPL) : générer un flux de test puis
l'envoyer directement, en brouillon noir pour limiter l'encre en cas d'erreur :

```sh
make check
build/mx870-bjnp send <ip> build/samples/sample_letter_plain_draft_gray.bin
```

## Arborescence

```
src/libmx870/   encodeur (commandes, bandes, PackBits, base 3, couleur, tramage, tables)
src/libbjnp/    client BJNP (découverte, identifiant, session, envoi acquitté)
src/cups/       filtre CUPS rastertomx870
src/app/        application PAPPL
src/tools/      mx870-bjnp (discover / id / send / relay)
tools/          bjdump.py (décodeur), preview.py, générateurs de tables et de PPD
tests/          tests unitaires, e2e CUPS, simulateur BJNP, test de l'application
packaging/      plists launchd
vendor/         binaires Canon, décompilation, Gutenprint, PAPPL (ignoré par git)
```

Les tables `src/libmx870/modes.inc` et `papers.inc` sont régénérées depuis votre copie
du pilote Windows par `make modes`.
