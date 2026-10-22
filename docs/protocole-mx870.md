# Canon MX870 — notes de rétro-ingénierie du pilote Windows

Source analysée : `mp68-win-mx870-1_06-ea24.exe` (SHA-256 `464d5dcd…7e14`), *MX870 series
MP Driver Ver.1.06 (Windows)* publié par Canon
([asia.canon/en/support/0100462001](https://asia.canon/en/support/0100462001)), pilote
d'impression x86 v2.56.2.10 (INF du 29/05/2012). Ni les binaires, ni le code décompilé, ni
les tables extraites ne sont versionnés : ce document ne contient que des observations, et
les tables (§4.4) sont régénérées par chacun depuis sa propre copie de l'installeur
(`install.sh`, `tools/extract_assets.sh`).

Légende : **[obs]** observé dans les octets/le pseudo-code, **[inf]** inférence, **[?]** inconnu.

**Outils et références**

| Outil / source | Usage ici |
|---|---|
| [REA](https://github.com/morluto/rea) piloté par [Claude Code](https://claude.com/product/claude-code) | inspection PE, ressources, recherche de chaînes, décompilation |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 12.1 (+ [Temurin](https://adoptium.net) 25) | décompilation, via REA et en mode *headless* (export de tout le pseudo-code) |
| [Hopper](https://www.hopperapp.com) | essayé en premier, abandonné (Python indisponible) |
| [Gutenprint](https://gimp-print.sourceforge.io) | pilote libre pour des modèles Canon proches, utilisé pour recouper le sens des commandes |
| [cups-bjnp](https://sourceforge.net/projects/cups-bjnp/) | implémentation libre de BJNP, cohérente avec §4.6 |
| `unzip`, `tar` ([libarchive](https://www.libarchive.org)) de macOS | extraction du [WinZip Self-Extractor](https://www.winzip.com/en/product/self-extractor/) et des CAB |
| [PackBits](https://en.wikipedia.org/wiki/PackBits) | algorithme de compression reconnu dans `cnmop9z.dll` |

## 1. Emballage

| Couche | Format | Outil |
|---|---|---|
| `.exe` | WinZip Self-Extractor (ressource `WZ_MANIFEST`) + ZIP en overlay [obs] | `unzip` |
| `Driver/PRNG3.cab` (x86), `PRNG6.cab` (x64), `PrnCm.cab` (données) | Microsoft CAB [obs] | `tar -xf` (bsdtar) |

Identifiant Plug-and-Play : `USBPRINT\CanonMX870_series946F` [obs].

## 2. Architecture du pilote (chaîne d'impression)

```
GDI ──► cnmdrv.dll (DrvEnableDriver, pilote de rendu)
          │  DmpStartDoc / DmpStartPage / DmpEndPage / DmpEndDoc
          ▼
        cnmdump5.dll  ── pipeline image : couleur (ICM + cnb_3670.tbl), tramage
          │  charge dynamiquement le module de sortie (OutputModuleID-99)
          ▼
        cnmop9z.dll   ── encodeur du flux raster BJ (OutputOpen/Init/Produce/FormFeed/Close)
          │
          ▼
        cnmlmon2.dll  ── Language Monitor : BJL, statut, dialogue bidirectionnel USB/réseau
```

| Fichier | Rôle | Contenu |
|---|---|---|
| `cnm_0367.dll` | Base de données modèle (`CPD_Access*DataBase`) | Gabarits de commandes (`ESC (p`, `ESC (P`, `ESC (t`…), blocs BJL [obs] |
| `cnmp_367.dll`, `cnmbr367.dll` | Données modèle | Uniquement `.rsrc`, aucun code [obs] |
| `cnmpcom2.dll` | Données communes | 40 `RT_BITMAP` de 59 066 o (ID 28001‑28136) → matrices de tramage [inf] |
| `cnb_3670.tbl`, `CNBA7*.ICM` | Tables couleur | LUT/profils ICC par support [inf] |

## 3. Paramètres internes du moteur (cnmdump5.dll) [obs]

Dictionnaire `Nom-ID` : ColorBalance‑93, BJESThreadNum‑94, TablePath‑95, CalibrationFlag‑96,
CalibrationData‑97, AutoHalftone‑98, OutputModuleID‑99, OutputProcessID‑100,
BalanceCyan/Magenta/Yellow/Black/Density‑101…105, ColorAdjustMode‑106, Contrast‑107,
RGB2GrayConvert‑108, MonochromeTone/C/M/Y‑109…112, MachineID‑113, OutputInitData‑114,
ModelID‑115, MediaType‑116, InkCartridge1/2‑117/118, PrintSystem‑119, ImageFormat‑120,
PrintQuality‑121, HalftoneMethod‑122, GrayScale‑123, InputResolutionH/V‑124/125,
OutputResolutionH/V‑126/127, BannerMode‑128, TableName‑129, RenderingIntent‑130,
InputGamma‑131, PaperSizeID‑132, PrintPosition‑133, PaperSizeH/V‑134/135, CartridgeSet‑136,
MarginType‑137, DuplexPrinting‑138, PaperGapCommand‑139, SingleColorMode‑140,
EmergencyMode‑141, PrintWidth‑142, MaxRaster‑143, 3DPrint‑144, ChartPrint‑145,
SpecialApplicationType‑146.

C'est l'inventaire des fonctionnalités à couvrir dans la réécriture.

## 4. Flux envoyé à l'imprimante

### 4.1 Enveloppe BJL [obs, cnm_0367.dll / cnmlmon2.dll]

Préfixe binaire `1B 5B 4B 02 00 00 1F` (`ESC [K` len=2 + `0x1F`), puis texte :

```
BJLSTART
ControlMode=Common
SetTime=yyyymmddhhmmss      (ou SetSilent=ON|OFF)
BJLEND
```

### 4.2 Commandes raster `ESC ( <lettre> <len 16 bits LE> <données>` [obs]

| Cmd | len | Exemple / gabarit | Sens probable |
|---|---|---|---|
| `ESC (s` | var | `01 00 00 "MX870 series"` | identification modèle [obs] |
| `ESC (c` | 3 | `30 00 01` (30 variantes) | méthode d'impression / support / qualité [inf] |
| `ESC (d` | 4 | `01 2C 01 2C` | résolution H,V en big-endian = 300×300 dpi [inf] |
| `ESC (l` | 3 | — | source papier (bac / cassette) [inf] |
| `ESC (P` | 8 | 22 variantes | type/format de papier [inf] |
| `ESC (p` | 46 | 62 variantes | géométrie de page (marges, zone imprimable) [inf] |
| `ESC (t` | 42 | `80 00 01 01 00 02 …` | configuration raster : bits/niveaux par encre [inf] |
| `ESC (q` | 1 | `01` | [?] |
| `ESC (u` | 1 | `01` | [?] |
| `ESC (b` | 1 | `00`/`01` | compression des données raster [inf] |
| `ESC (e` | 2 | `00 00` | [?] |
| `ESC (F`, `(I`, `(J` | 0‑1 | — | gabarits de `cnmop9z.dll` [?] |
| `ESC (L` | 1‑8 | `"K"`, `"k"`, `"Kk"`, `"CMY"`, `"CMYK"`, `"CMYk"`, `"CMYKk"`, `"CMYcmk"` | ordre des plans couleur [obs] ; `K` vs `k` = noir pigment (PGBK) vs noir colorant (CLI‑221BK) [inf] |

### 4.3 Couche raster (cnmop9z.dll) [obs dans le pseudo-code, sauf mention]

Module de sortie chargé par `cnmdump5.dll` via `GetProcAddress` : `OutputOpen`, `OutputInitTwo`,
`OutputInitPart`, `OutputProduce`, `OutputComplete`, `OutputFlush`, `OutputFormFeed`, `OutputClose`.

Organisation du flux de page :

```
[ESC (L n <lettres>]            si l'ensemble des plans présents change (FUN_66522e87)
[ESC (e 02 00 <n BE16>]         saut de n lignes blanches, n ≤ 0x7FFF, répété si besoin (FUN_665221db)
ESC (F <len LE16> <lignes>      un bloc par plan couleur, dans l'ordre de ESC (L
...
ESC (I 01 00 01                 fin de page (OutputFlush / OutputFormFeed) [inf]
ESC (J 01 00 <v>                v = valeur d'état du module [?]
```

* **Bande** : un `ESC (F` contient 1, 8 ou 16 lignes (`+0x7e8`) : 8 à 300 dpi, 16 à 600 dpi
  sauf certains modes, 1 sinon (`OutputInitPart`). La longueur LE16 (octets 3‑4) est
  corrigée après remplissage (`FUN_6652232e`).
* **Ligne** : `[marge gauche] PackBits(données) 0x80`. Ligne vide = `0x80` seul.
  La marge gauche (octets nuls) est codée en runs `81 00` (128 zéros) puis `(1-n) 00`
  (`FUN_66521460`). `0x80` sert de fin de ligne.
* **Compression** : PackBits standard (`FUN_665214b8`) : `0..127` → n+1 octets littéraux ;
  `-1..-127` → répéter l'octet suivant 1‑n fois (max 128).
* **Profondeur** : 1, 2 ou 4 bits/pixel selon le plan (`8 >> code`, code 3/2/1).
  Les plans 2 bits à 3 niveaux sont compactés **5 pixels → 1 octet** via une table de
  1024 entrées à `0x665260e0` (`FUN_665245e2`, 10 bits lus → 1 octet ; 3⁵ = 243).
* **Bornage** : seuls les octets entre le premier et le dernier pixel non nul sont émis
  (`FUN_6652229e`, `FUN_6652429c`).
* Tables de correspondance niveau de trame → code de point : `0x66526088…0x665260d3`.

### 4.4 En-têtes de mode et de page

Enregistrements de mode dans `cnm_0367.dll` (≈0x16de1…0x175a3) : `ESC (c` + `ESC (d` + `ESC (t`
+ nom de profil ICC (`cnba7mb0.icm`…) [obs].

| Cmd | Charge utile | Source |
|---|---|---|
| `ESC (c` 3 | `0x30` (= model_id 3 << 4, `|1` si noir seul), code support, qualité 1‑4 | [obs] Windows, sens [inf] via Gutenprint |
| `ESC (d` 4 | dpi H (BE16), dpi V (BE16) : 600×600, ou 300×300 en brouillon | [obs] |
| `ESC (t` 3+7×3 | en-tête `80 80 01` (`80 00 01` en 300 dpi brouillon), puis par encre C M Y K c m k : `[0x20 si 5 px/octet | bits, 00, niveaux]` | [obs] Windows, ordre [inf] confirmé par Gutenprint (`INKSET 13_C3M3Y2K2…`) |
| `ESC (l` 3 | `0x30|bac`, code support, 0 | [obs] gabarits `3e/34/38/3f`, sens via Gutenprint |
| `ESC (P` 8 | `00 <format> 00 <support> <orient1> <orient2> 01 00` | longueur 8 [obs] Windows v2.56 ; Gutenprint utilise 6 |
| `ESC (p` 46 | BE : long. imprimable(2) 0(2) larg. imprimable(2) 0(2) 0(4) unité=600(2) zone_x(4) zone_y(4) zone_l(4) zone_h(4) 0(4) 0(4) papier_l(4) papier_h(4) | [obs] 62 gabarits, disposition [inf] via Gutenprint |
| `ESC (q` 1 | `01` (identifiant de page) | [obs] |
| `ESC (I` 1 / `ESC (J` 1 | raster multiligne activé / lignes par bloc | [obs] cnmop9z + Gutenprint |
| `ESC (b` 1 | `01` = PackBits activé ; `00` en fin de page | [obs] |
| `0x0C` | éjection de page | [obs] gabarit fin de page cnm_0367 |
| `ESC @` | fin de travail | Gutenprint |

**Codes de format `ESC (P`** [obs, 21 valeurs identiques entre Windows et Gutenprint] :
A5 `01`, A4 `03`, B5 `08`, Letter `0d`, Legal `0f`, Hagaki `14`, Env #10 `2e`, DL `2f`,
You4 `30`, You6 `31`, L `32`, 2L `33`, 4×6 `34`, carte 55×91 `36`, 5×7 `37`,
Oufuku `39`, Chou3 `3a`, Chou4 `3b`, 4×8 `46`, 8×10 `49`, Wide `52`.

**Géométrie papier (1/600 po) des gabarits `ESC (p`** [obs] : A4 4961×7016, Letter 5100×6600,
Legal 5100×8400, A5 3497×4961, B5 4300×6071, 4×6 2400×3600, 5×7 3000×4200, 8×10 4800×6000…
Zone imprimable maximale : 4800 points (8 po). Marges avec bord : x=80 (151 en Letter/Legal),
y=70 ou 118. Sans bord : décalages −70/−118 ou −66/−47.

**Table 10→8 bits** : vérifiée sur les 243 combinaisons : `octet = Σ pᵢ·3^(4−i)`
(pixel 0 = poids fort). Implémentable sans la table Canon.

### 4.5 Enveloppe du travail

Le pilote Windows contient des blocs BJL (§4.1). Le Language Monitor `cnmlmon2.dll`
manipule aussi des commandes XML IVEC (`StartJob`, `VendorCmd/ModeShift`, `EndJob`,
`GetStatus`) [obs, chaînes]. Gutenprint envoie pour la MX870 :

```xml
<?xml version="1.0" encoding="utf-8" ?><cmd xmlns:ivec="http://www.canon.com/ns/cmd/2008/07/common/"><ivec:contents><ivec:operation>StartJob</ivec:operation><ivec:param_set servicetype="print"><ivec:jobID>00000001</ivec:jobID><ivec:bidi>0</ivec:bidi></ivec:param_set></ivec:contents></cmd>
<?xml …?><cmd …ivec… xmlns:vcn="http://www.canon.com/ns/cmd/2008/07/canon/"> … <vcn:ijoperation>ModeShift</vcn:ijoperation><vcn:ijmode>1</vcn:ijmode> … </cmd>
… flux raster …
<?xml …?><cmd …><ivec:operation>EndJob</ivec:operation> … </cmd>
```

## 4.6 Réseau : BJNP (LAN/CNMNPPM.DLL, port monitor « bjnpmon » v3.1.1)

**En-tête** (16 octets, big-endian, `FUN_1003d4e0`) [obs] :

| Offset | Taille | Champ |
|---|---|---|
| 0 | 4 | `"BJNP"` |
| 4 | 1 | type d'appareil : `0x01` imprimante (port 8611), `0x02` scanner (8612) ; `|0x80` en réponse |
| 5 | 1 | code de commande |
| 6 | 2 | code d'erreur (0 = OK, vérifié dans les réponses) |
| 8 | 2 | numéro de séquence (compteur global incrémenté) |
| 10 | 2 | identifiant de session (rendu par `0x10`) |
| 12 | 4 | longueur de la charge utile |

**Commandes** (`FUN_10040d30` : proto `0x11` = UDP, `6` = TCP) [obs] :

| Code | Transport | Charge utile | Rôle [inf] |
|---|---|---|---|
| `0x01` | UDP | — | découverte |
| `0x30` | UDP | — | identifiant IEEE 1284 |
| `0x10` | UDP | 392 o : 4+4 o, nom machine (64 o UTF‑16), utilisateur (64 o), titre (256 o) | ouverture du travail ; la réponse donne la session (en-tête, offset 10) |
| `0x21` | TCP | données d'impression | réponse : u32 = octets acceptés ; 0 → occupé, réessai toutes les 500 ms, abandon après 10 s |
| `0x20` | TCP | — | lecture bidirectionnelle (statut) |
| `0x11` | UDP | — | fermeture de la session |
| `0x12`, `0x31`, `0x50`, `0x51` | UDP | divers | non utilisés pour l'impression |

Implémentation : `src/libbjnp` + outil `mx870-bjnp` (découverte, identifiant, envoi d'un fichier).

## 5. Implémentation et points à valider sur l'imprimante

Implémenté dans `src/libmx870` (encodeur), `src/cups` (filtre [CUPS](https://openprinting.github.io/cups/) + PPD généré), `src/libbjnp` et `src/app` (application [PAPPL](https://github.com/michaelrsweet/pappl)).
Les flux produits sont validés par `tools/bjdump.py` (décodeur indépendant) ; **aucun n'a encore
été envoyé à une MX870 réelle**. Choix non confirmés par les octets Windows, à vérifier en premier
si l'imprimante rejette un travail :

| Point | Choix actuel | Alternative |
|---|---|---|
| Enveloppe | XML IVEC StartJob/ModeShift/EndJob (Gutenprint) | BJL seul |
| `ESC (P` | 8 octets (Windows v2.56) | 6 octets (Gutenprint) |
| `ESC (r 64` | envoyé (Gutenprint) | absent des gabarits Windows |
| Lignes par bloc | 16 à 600 dpi, 8 à 300 dpi | Windows utilise 8 dans certains modes 600 dpi |
| Marge haute 118 | supposée liée au recto-verso | sens réel inconnu |
| Mode 7 (Pro II qualité 4) | exclu | plans supplémentaires `C|0x80`… non décodés |

Réseau (BJNP, §4.6) : validé contre `tests/fake_bjnp.py`, écrit d'après la même lecture du
protocole ; à confirmer sur l'imprimante : encodage UTF‑16 LE des champs de `0x10` (Windows)
alors que d'autres implémentations utilisent BE, taille des blocs `0x21` (16 Kio choisis).

Restent à faire : Language Monitor (`cnmlmon2.dll` : statut, niveaux d'encre via IVEC
`GetStatus`), étalonnage couleur sur tirages réels.
