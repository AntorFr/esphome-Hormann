# Émuler une UAP1 Hörmann sur ESP32 — retour d'expérience

Guide **pratique et condensé** pour qui veut refaire le même chemin : faire passer un ESP32
(composant `hormann_hcp1`) pour une **UAP1** sur le bus HCP1 d'un opérateur Hörmann, afin de
**lire l'état** de la porte **et la commander**.

Cible validée : **WeAct CAN485 DevBoard** (ESP32 + transceiver RS485 **isolé CA-IS2092A**).
Résultat : **émulateur UAP1 complet** — monitoring (porte/lumière/erreur/aération) **et**
commande (impulse / open / close / light / venting / stop). **La porte s'ouvre.** ✅

> Le journal chronologique complet (toutes les sessions, fausses pistes incluses) est dans
> [`../INVESTIGATION.md`](../INVESTIGATION.md). Ce fichier-ci est la version « ce qu'il faut
> savoir » pour ne pas reperdre les jours qu'on a perdus.

---

## TL;DR — les 2 verrous qui coûtent le plus

1. **RX muet sur un transceiver ISOLÉ** → il faut **inverser physiquement A/B** (+ `ab_inverted: true`).
2. **Commande ignorée (le maître scanne mais n'enregistre jamais)** → le **sync break est trop court**.

Si tu ne lis que ça : applique ces deux fixes et teste.

---

## Le bus & le matériel

- **Bus HCP1** = RS485 **19200 8N1**, half-duplex. Connecteur opérateur (selon modèle) :
  `pin 2 = 24VDC`, `pin 3 = GND`, `pin 5 = RS485 DATA- (B)`, `pin 6 = RS485 DATA+ (A)`.
- **WeAct CAN485** : transceiver **CA-IS2092A isolé** (DC-DC isolé intégré, `VDD5V_ISO` généré
  par la puce). RS485 : `DI(TX)=GPIO22`, `RO(RX)=GPIO21`, `DE=GPIO17`. WS2812=GPIO4, KEY=GPIO0.
- **Switches de la carte** : **PUPD = OFF**, terminaison 120 Ω = OFF (le maître biaise et termine déjà).

---

## Verrou 1 — RX : `0 valid, 0 junk, 0 breaks` sur la carte isolée

**Symptôme** : le sniffer ne voit **rien** (même pas de breaks), alors qu'un module **non-isolé**
(SP3485/ST485) branché sur **les mêmes fils** décode parfaitement (~280 trames, CRC OK).

**Diagnostic** (au scope, réf. GND_ISO) : ce bus Hörmann **drive de façon asymétrique** —
**DATA+ (pin 6) fait tout le swing** (−0,4 → +3,2 V), **DATA- (pin 5) reste collé à GND**.
Le différentiel existe (porté par A) ; un transceiver non-isolé le décode. Mais le
**CA-IS2092A isolé a un fail-safe interne agressif** qui lit le niveau « espace » faible (−0,4 V)
comme **idle permanent** → sa sortie RO reste figée → 0/0/0.

**Solution** :
- **Inverser PHYSIQUEMENT A et B** à l'entrée de la carte : `pin 6 (DATA+) → borne B-`,
  `pin 5 (DATA-) → borne A+`. Ça met le swing fort côté « espace » → le récepteur accroche.
- La sortie RO est alors **logiquement inversée** → compenser avec **`ab_inverted: true`**
  (inverse RX **et** TX ensemble, c'est une seule paire différentielle).

> Un transceiver **non-isolé** (ST485/SP3485, comme stephan192) n'a **pas** ce problème : il
> décode le bus en **polarité normale** (`ab_inverted: false`), sans swap. Le piège est
> spécifique au fail-safe des transceivers **isolés**.

**Astuce de diagnostic** : la mesure qui tranche « câble vs polarité vs HW » = scoper **A+ vs GND**
puis **B- vs GND**. Si **une seule ligne bascule**, le différentiel est « demi » → c'est le cas
asymétrique ci-dessus.

---

## Verrou 2 — Commande : le maître scanne 0x28 en boucle mais n'escalade jamais

**Symptôme** : le maître **scanne** notre adresse (`28:..:01:..`, souvent `+N identical` = il
**remartèle**), on **répond** (le witness voit `OUR-SCANRESP` propre), **mais** il ne passe
**jamais** au `status_request` (0x20) → on n'est **pas enregistré** → `impulse` ignoré.

**Ce qui était DÉJÀ bon** (vérifié byte-à-byte contre une vraie trace UAP1 **et** stephan192) :
scan-réponse `80:12:14:28`, status-réponse `80:..:29:00:10`, compteur, **timing de réponse 3840 µs**,
drive 5 V. **Le protocole n'était pas en cause.**

**LA cause** : le **sync break** (le `0x00` de tête qui précède chaque trame) était **2× trop court**.

| source | break (niveau bas) | bits @19200 | marche ? |
|---|---|---|---|
| **vrai UAP1** (mesuré sur trace Saleae) | **~920 µs** | **~18 bits** | réf |
| **hgdo** (`0x00` @ 9600 7N1) | ~833 µs | ~16 bits | ✅ |
| **notre ancien code** (`0x00` @ 19200) | **~470 µs** | **~9 bits** | ❌ |

Le maître **strict** ne reconnaissait pas le début de notre trame → ne la **framait** pas → ne
nous enregistrait pas. (Un récepteur tolérant comme notre propre witness, lui, décodait — d'où la
confusion.)

**Solution** (dans `send_frame`, méthode hgdo) : émettre le `0x00` du break à **moitié baud
(9600)** = ~937 µs de bas (~18 bits @19200), **puis repasser à 19200** pour la trame.
`uart_set_baudrate` est rapide (simples écritures registre — le mythe « 40 ms » est faux).

**Résultat immédiat** : le maître **escalade** vers `status_request` toutes les ~100 ms,
`impulse` → status `80:..:29:**04**:10` (data 0x1004) → **la porte s'ouvre**.

---

## ⚠️ Fausses pistes — ne perdez PAS de temps dessus

- **Le timing à la µs n'est PAS critique.** `hgdo` (ESP8266 + SoftwareSerial) répond avec un
  délai en **millisecondes** (`millis()`, `TX_DELAY = 3 ms`, jitter ms) et pilote la porte.
  Inutile de chercher un micro dédié / un timing cyclé. ~3,8 ms ±1 ms suffit.
- **PUPD : à laisser OFF.** Le biais ajouté par la carte **perturbe TOUT le bus** (même un
  witness passif devient aveugle). La **120 Ω** de la carte, elle, est **neutre côté RX**.
- **L'inversion logicielle seule ne suffit pas** sur l'isolé : `ab_inverted` n'inverse que l'UART
  **après** la puce. Le fail-safe de la puce agit sur l'**analogique** → il faut le **swap physique**.
- **L'annonce série au boot n'est PAS requise.** Un vrai UAP1 diffuse son n° de série au démarrage,
  mais **stephan192 ne l'implémente pas** et commande quand même. Inutile de la reproduire.
- **Méfiez-vous de votre propre witness.** Le nôtre **émettait** sa propre scan-réponse (oubli de
  `listen_only`) et **polluait le bus** → il faut un oracle **vraiment passif** (`listen_only: true`).
- **La latence vue par l'oracle est faussée** quand le maître remartèle le scan (`+N identical`) :
  mesurez depuis le **premier** req de la rafale, pas le dernier (cf. fix dans le composant).

---

## Méthodo & outils (dans ce dossier)

- **`sniffer: true`** (option du composant) : logue les trames du bus catégorisées
  (`SCAN->us`, `STATUS_REQ->us`, `BCAST`, `OUR-SCANRESP`, `OUR-STATUSRESP`) + un heartbeat
  `N valid / N junk / N breaks`. Premier réflexe pour voir ce qui se passe.
- **Witness oracle** : une 2ᵉ carte en **`listen_only: true`** (n'émet jamais) qui observe tout
  et flague **nos** réponses (`OUR-SCANRESP<<<`) + la latence réelle. Indispensable pour confirmer
  que notre TX atteint le bus proprement, **sans** être nous-même un participant.
- **[`track_analyse/decode_uart.py`](track_analyse/decode_uart.py)** : décode une **capture
  logique (Saleae)** export CSV en **trames Hörmann** (19200 8N1), TX et RX séparés, avec timing
  et durée des breaks. C'est ce qui nous a donné le **break ~920 µs** et confirmé que nos trames
  étaient byte-identiques à un vrai UAP1.
  ```bash
  python3 investigation/track_analyse/decode_uart.py investigation/track_analyse/UAP1-startup.csv
  ```
- **[`track_analyse/UAP1-startup.csv`](track_analyse/)** : capture Saleae du **démarrage d'un
  vrai UAP1** (référence pour comparer scan/status/break/timing). Source : traces de bouni.

---

## La config qui marche (extrait `garage-can485.yaml`)

```yaml
# Câblage bus -> carte : pin6 DATA+ -> borne B-, pin5 DATA- -> borne A+ (INVERSÉS), pin3 GND -> Mass.
# Switches carte : PUPD OFF, 120 Ω OFF.
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: GPIO22       # DI
  rx_pin: GPIO21       # RO
  de_pin: GPIO17       # DE (actif-haut)
  ab_inverted: true    # A/B inversés physiquement -> ré-inversion logicielle RX+TX
  reply_delay_us: 3800 # latence de réponse calée sur un vrai UAP1 (~3.84 ms)
  sniffer: false       # true = debug
```

---

## Références

- **[stephan192/hoermann_door](https://github.com/stephan192/hoermann_door)** — émulateur UAP1 de
  référence (PIC16 pour le bus + ESP8266). Constantes & framing identiques aux nôtres ;
  **pas d'annonce série** ; transceiver **ST485 non-isolé** + 120 Ω.
- **[steff393/hgdo](https://github.com/steff393/hgdo)** — ESP8266 + SoftwareSerial. Prouve que le
  **timing en ms suffit** et utilise un **break long** (baud-switch 9600). C'est lui qui nous a mis
  sur la piste du sync break.
- **[`../INVESTIGATION.md`](../INVESTIGATION.md)** — le journal complet, chronologique, avec toutes
  les sessions et les détails électriques.
