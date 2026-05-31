# Hörmann Supramatic E3 — Investigation HCP / UAP1

Synthèse complète de l'investigation pour intégration ESPHome via bus RS485.

## TL;DR

- ✅ **Lecture d'état fonctionnelle** : broadcasts décodés, état porte (open/closed/opening/closing/venting), light, error, prewarn remontés dans Home Assistant.
- ❌ **Commande non fonctionnelle** : le master Supramatic E3 ne demande jamais le `status_request` censé déclencher l'envoi de nos commandes (impulse, open, close, light…).
- 🔬 **TX hardware vérifié OK** via 2ème ESP témoin sur le bus : nos octets arrivent physiquement sur la ligne RS485, mais soit l'instant où on émet collisionne avec les broadcasts du master, soit le master n'attend simplement pas notre réponse de la même façon que les Supramatic anciens.

---

## 1. Implémentations de référence consultées

| Repo | Cible matériel | Langage | Take-away principal |
|------|----------------|---------|---------------------|
| **stephan192/hoermann_door** | Supramatic 4 / LineaMatic / Pic16+ESP8266 | C (PIC16) + C++ (ESP) | Référence "canonique". PIC16 dédié pour le timing/break, ESP juste UART simple par-dessus. Délai 3 ms via Timer0 1ms. Break TX via `SENDB=1; TX1REG=0x00`. CRC table + format protocole. |
| **steff393/hgdo** | Supramatic / ESP8266 direct (sans UAP1) | C++/Arduino | Implémente *exactement* le même protocole en pur ESP. **Trick génial pour le break TX** : switch UART à 9600 7N1, écrit `0x00` (= ~1.04 ms low = ~20 bit-times à 19200), revient à 19200 8N1, envoie le frame. `cfgMasterAddr` configurable. |
| **raintonr/hormann-hcp** | LineaMatic P / Linux + USB-RS485 | Node.js | Même break trick que steff393 (`port.update({baudRate: 9600, dataBits: 7})`). **Aucun delay** entre RX du scan et TX de la réponse. `icAddress = 0x28`, type = `0x14`. |
| **Gifford47/HCPBridgeMqtt** | Supramatic E4 | C++ | Modbus RTU (totalement différent). Slave ID 2, registres 16 bits. **Pas applicable au E3** mais montre que Hörmann utilise plusieurs protocoles selon génération. |
| **mapero/esphome-hcpbridge** | Hörmann E4 (variante du précédent) | ESPHome | Idem, Modbus. |
| **ljames8/hormann-hcp-client** | Generic | TypeScript | Client TS, même protocole HCP1 que stephan192. |
| **bouni blog** ([blog.bouni.de/posts/2018/hoerrmann-uap1/](https://blog.bouni.de/posts/2018/hoerrmann-uap1/)) | Reverse engineering UAP1 | — | Source de doc protocole. Confirme : addr 0x28 UAP1, type 0x14, master 0x80 (drive) ou 0x8D (device), CRC poly 0x07 init 0xF3, baud 19200 8N1. **Mais doc incomplète** : ne décrit pas la transition scan → status_request ni le timing exact attendu par le master.

---

## 2. Protocole HCP1 (selon références)

### Adresses
- `0x00` : Broadcast (master émet l'état porte vers tous)
- `0x80` : Master drive
- `0x8D` : Master device (rôle exact pas clair, parfois utilisé comme master alternatif)
- `0x28` : UAP1 slave (notre rôle)
- `0x10..0x90` : autres slaves possibles (cellules, panneaux, etc.)

### Format de frame
```
[ADDR_DEST] [CNT|LEN] [PAYLOAD...] [CRC8]
```
- `CNT|LEN` : nibble haut = compteur (0-15), nibble bas = longueur du payload
- `CRC8` : poly `0x07`, init `0xF3`, calculé sur tous les bytes (résultat sur frame complète = 0)

### Séquence attendue selon les refs
1. Master scan : `[0x28][CNT|0x02][0x01][0x80][CRC]`
2. Slave répond : `[0x80][CNT+1|0x02][0x14][0x28][CRC]` (notre adresse + type UAP1)
3. **Si scan accepté** → master envoie status_request : `[0x28][CNT|0x01][0x20][CRC]`
4. Slave répond : `[0x80][CNT+1|0x03][0x29][LOW][HIGH][CRC]` où `LOW|HIGH` = code action (0x1000 = idle, 0x1001 = open, 0x1002 = close, 0x1004 = impulse, 0x1008 = light, 0x1010 = venting, 0x0000 = stop)
5. Master émet broadcast périodique : `[0x00][CNT|0x02][D0][D1][CRC]` avec D0/D1 = bits d'état

### Break (sync) entre frames
Le master émet un sync break (~12 bit-times de ligne low = ~625 µs à 19200) avant chaque frame, et attend le même de la part du slave.

---

## 3. Implémentation ESPHome actuelle

### Architecture
- Composant custom `hormann_hcp1` en C++ pur (`components/hormann_hcp1/`)
- Framework **ESP-IDF natif** (pas Arduino)
- UART driver ESP-IDF avec event queue dédiée
- Task FreeRTOS dédiée (priorité 23, core 1) pour parser les frames
- Plateformes : cover, light, binary_sensor, button

### Configuration YAML
```yaml
hormann_hcp1:
  id: hormann
  uart_num: 1
  tx_pin: 17       # → DI du module RS485
  rx_pin: 19       # → RO du module RS485 (pas GPIO18 sur S3 = USB)
  de_pin: GPIO4    # → EN du module RS485 (DE/RE combinés)
  slave_addr: 0x28
  master_addr: 0x80
  slave_type: 0x14
  auto_scan: false  # mode test cyclant 12 combinaisons addr/type
  de_invert: false
```

### Détails techniques implémentés et validés
- ✅ CRC8 0x07/0xF3 conforme aux refs
- ✅ Détection break RX via `UART_BREAK` event ESP-IDF
- ✅ Parser tolérant : scanne tous les offsets du buffer pour trouver une frame valide (gère les bytes parasites en début de chunk)
- ✅ Décodage broadcast : cover state, light, error, venting, prewarn, option_relay
- ✅ Trigger callbacks vers cover/light/binary_sensor uniquement sur changement d'état
- ✅ Logger optimisé (WARN level) pour ne pas ralentir la task bus

### Points sensibles de l'implémentation TX
- Mode RS485 : testé avec et sans `UART_MODE_RS485_HALF_DUPLEX`. Conclusion : **mode auto coupe DE trop tôt** (bytes tronqués). Retour à contrôle manuel DE + délai 600 µs après `uart_wait_tx_done` avant de baisser DE.
- Break TX : 3 méthodes essayées :
  1. ❌ `uart_set_line_inverse(UART_SIGNAL_TXD_INV)` + délai → fonctionne mais master ne reconnaît pas
  2. ❌ Baud-switch 9600 7N1 + `0x00` (méthode steff393/raintonr) → trop lent (~40 ms/TX à cause des `uart_set_baudrate`)
  3. ✅ Préfixe `0x00` à 19200 8N1 (= ~470 µs low) → rapide (~3 ms total) mais peut-être insuffisant comme break valide

---

## 4. Observations terrain (Supramatic E3)

### Trafic du bus observé
```
Frames cycliques toutes ~5 s :
  00:00:X2:02:02:CRC    ← broadcast périodique (état porte fermée : d0=0x02, d1=0x02)
  00:28:82:01:80:06     ← scan vers nous (UAP1 0x28)
  00:28:02:01:80:0D     ← variante avec compteur autre
  
Plus rarement (mystérieuses) :
  00:80:22:01:80:01     ← scan adressé à 0x80 (master drive ?)
  00:80:A2:01:80:0A     ← idem variante compteur

Au démarrage, balayage complet d'adresses 0x10 à 0x90 (master cherche les slaves présents).
```

### Ce qu'on a confirmé
- ✅ La carte principale RX bien tous les broadcasts → on peut décoder état porte
- ✅ Le master scanne régulièrement 0x28 → notre slot UAP1 est reconnu comme adresse à scanner
- ✅ Notre TX **arrive physiquement** sur le bus (preuves multiples côté témoin) :
  - Test `tx_diag` envoyant 16 × `0xAA` → octets visibles côté témoin
  - Frames du master corrompues juste après nos `send_frame` (preuve d'overlap RS485)
- ✅ Latence de notre `send_frame` réduite à **~3.3 ms** après optimisation

### Ce qu'on observe MAIS qu'on n'arrive pas à corriger
- ❌ **Notre scan reply n'est jamais reçue proprement** par le témoin (jamais de chunk `<<< 80:..:14:28:CRC` clean)
- ❌ Au lieu de ça, on voit le broadcast suivant **CORROMPU** ~20-50 ms après notre TX :
  ```
  Attendu :  00:00:92:02:02:62  (broadcast cnt=9 normal)
  Observé :  00:C0:C9:86:62:62  ou  00:32:D9:02:02:62  ou  00:FC:8A:96:02:02:62
  ```
- ❌ **Le master ne transitionne jamais vers status_request** — quel que soit le slave_addr, master_addr, slave_type, timing testé.

### Tests addr/type/timing épuisés
Cyclage automatique sur 12 combinaisons (`auto_scan: true`) sur 3 minutes :

| # | slave | master | type | Résultat |
|---|-------|--------|------|----------|
| 1 | 0x28 | 0x80 | 0x14 | Pas de status_request |
| 2 | 0x29 | 0x80 | 0x14 | Pas de status_request |
| 3 | 0x82 | 0x80 | 0x14 | Pas de status_request |
| 4 | 0x83 | 0x80 | 0x14 | Pas de status_request |
| 5 | 0x81 | 0x80 | 0x14 | Pas de status_request |
| 6 | 0x28 | 0x8D | 0x14 | Pas de status_request |
| 7 | 0x82 | 0x8D | 0x14 | Pas de status_request |
| 8 | 0x28 | 0x80 | 0x10 | Pas de status_request |
| 9 | 0x28 | 0x80 | 0x12 | Pas de status_request |
| 10 | 0x28 | 0x80 | 0x15 | Pas de status_request |
| 11 | 0x28 | 0x80 | 0x16 | Pas de status_request |
| 12 | 0x28 | 0x80 | 0x20 | Pas de status_request |

Délais essayés : 0 µs (immédiat) / 200 µs / 1.5 ms / 3 ms (recommandation PIC). Aucun ne change le comportement.

Polarité DE (`de_invert`) : testée à `true`, jamait le bus → la polarité par défaut (active-HIGH) est la bonne.

---

## 4 bis. Analyse hardware comparative (sessions ultérieures)

### Stephan192 (référence "carte propre")

Schéma `board/RS485Interface.sch` + BOM analysés. Composants clés :
- **IC4 = ST485BDR** — transceiver RS485 5V (SOIC8), niveau industriel
- **R5 = 120 Ω** — résistance de terminaison entre A et B (critique !)
- **R15/R16 = 100 Ω** en série sur A et B (protection ESD)
- C13/C14 = 100 µF aluminium → filtrage d'alim solide
- IC2 = AOZ1283PI step-down + IC3 = MCP1826S-3302 LDO → alim 5V/3.3V dédiée, isolée du bus 24 V
- D1 = SS25 diode Schottky → protection inverse polarité
- PIC16F15324 comme cœur (16-bit timers précis, ISR UART dédiée)

### HGDO (référence "ESP direct")

Code lu, image PCB consultée, wiki cloné en local. Points clés :
- **NodeMCU ESP8266** + module RS485 séparé (visible sur photo)
- Convertisseur DC-DC visible (probablement MP1584) + condo électrolytique
- **Méthode TX : SoftwareSerial (pas Hardware UART !)** → contrôle de timing au cycle CPU près
  ```cpp
  S.begin(9600, SWSERIAL_7N1);   // break baud-switch
  S.write(0x00);
  S.flush();
  S.begin(19200, SWSERIAL_8N1);   // retour normal
  S.write(txData, txLength);
  S.flush();
  ```
- Pinout configurable avec **inversion possible** des lignes TX/RX :
  ```cpp
  if (cfgHwVersion == 10) {
    S.begin(19200, SWSERIAL_8N1, PIN_DI, PIN_RO);  // inverted
  } else {
    S.begin(19200, SWSERIAL_8N1, PIN_RO, PIN_DI);
  }
  ```
- **`cfgMasterAddr` exposé en config** :
  ```c
  cfgMasterAddr  // Master address: 128 (0x80) per default, 144 (0x90) for HAP1-HCP-Adapter
  ```
  → **0x90 est une adresse master valide** pour certains modèles ! (Jamais testée dans notre auto_scan.)

### Notre setup (Supramatic E3 + ESP32 + module RS485 chinois)

- **Module RS485 cheap** avec broche EN unique (DE et /RE en interne combinés)
- Pas de schéma, pas de datasheet — probablement un MAX485 chinois sur PCB simple
- **Terminaison 120 Ω entre A/B : statut INCONNU** (à vérifier au multimètre)
- 3.3V depuis ESP32 (le module accepte 3.3V ou 5V selon variante)
- **Hardware UART ESP-IDF natif** (pas SoftwareSerial)
- Pas de protection ESD spécifique côté carte
- Référence GND commune avec le moteur via le bus (à vérifier)

### Tableau comparatif

| Aspect | stephan192 | hgdo | Notre setup |
|---|---|---|---|
| Transceiver | **ST485BDR** | (MAX485/SN65 ?) | Module HW-519 / auto-dir |
| Terminaison 120 Ω A↔B | **OUI (R5)** | Inconnu | **À vérifier** |
| Résistances série A/B | 100 Ω (R15/R16) | Inconnu | Aucune |
| Alim transceiver | 5V isolée propre | 5V NodeMCU | 3.3V probable |
| DE et /RE | **Séparés** | Combinés | Combinés ("EN") |
| Méthode TX | Hardware UART PIC + ISR | **SoftwareSerial** | Hardware UART ESP-IDF |
| Inversion TX/RX | Non | **Configurable** | Non |

### Conclusions hardware

1. **Terminaison absente** → cause probable n°1. Sans 120 Ω entre A/B, la ligne réfléchit, le master Supramatic peut recevoir des bits corrompus côté TX descendant et rejeter notre frame. **Vérification immédiate** : multimètre entre A et B au repos → doit afficher ~60 Ω (2 × 120 en parallèle) ou ~120 Ω. Si > 10 kΩ → pas de terminaison.

2. **SoftwareSerial vs Hardware UART ESP-IDF** : hgdo a probablement un timing plus prévisible. Notre Hardware UART + driver + queue FreeRTOS + scheduling peut introduire des jitter de quelques ms entre `write_bytes` et émission réelle. Sur un bus où le master n'attend que ~3-5 ms, c'est critique.

3. **`master_addr = 0x90`** non testé. À ajouter à l'auto_scan ou tester directement.

4. **Module auto-direction / EN combiné** est moins flexible qu'un module avec DE et /RE séparés. Notamment, on ne peut pas garder RX actif pendant TX pour vérifier l'écho et détecter les collisions.

---

## 5. Hypothèses pour la suite

### A0. (NOUVEAU) Master à `0x90` au lieu de `0x80`
**Pourquoi :** hgdo expose `cfgMasterAddr` configurable avec `0x80` (par défaut) ou `0x90` ("HAP1-HCP-Adapter"). Notre auto_scan a essayé 0x80 et 0x8D mais **pas 0x90**. Il est possible que la Supramatic E3 ait un master_device différent du master_drive qu'on voit émettre les scans.

**À tester :** simplement éditer le YAML avec `master_addr: 0x90` et observer si on a enfin un `Status request`.

### A1. (NOUVEAU) Pas de terminaison 120 Ω sur notre bus
**Pourquoi :** stephan192 a R5=120 Ω explicitement entre A/B. Notre module RS485 cheap n'a probablement pas de terminaison intégrée (ou elle est désactivée par défaut). Sans terminaison, sur quelques mètres de câble, les réflexions corrompent les bits du côté receiver. Le master Supramatic reçoit notre frame avec quelques bits flippés → CRC fail → rejet silencieux.

**À tester :**
- Mesurer la résistance entre A et B au multimètre. Bus alimenté = couper l'alim moteur d'abord.
  - ~60 Ω : terminaison correcte (2 × 120 Ω en parallèle aux extrémités)
  - ~120 Ω : terminaison à une seule extrémité (acceptable)
  - kΩ ou MΩ : pas de terminaison → ajouter une 120 Ω entre A et B sur le module RS485 (ou activer le jumper si présent)
- Si pas de jumper, souder une résistance 120 Ω en parallèle sur les broches A/B du module

### A2. (NOUVEAU) Hardware UART ESP-IDF moins prévisible que SoftwareSerial
**Pourquoi :** hgdo utilise SoftwareSerial sur ESP8266 — chaque bit est émis manuellement par CPU avec un timing prévisible au cycle près. Notre Hardware UART ESP-IDF a un driver, des buffers, et passe par le scheduler FreeRTOS — la latence entre `uart_write_bytes()` et l'émission physique réelle peut varier de 0 à plusieurs ms (et `uart_wait_tx_done()` retourne avant que le shift register termine).

**À tester :**
- Implémenter un mode "bit-bang TX" sur GPIO17 dans `send_frame` quand on doit émettre une réponse au master. Désactiver l'UART le temps du bit-bang, puis le réactiver pour la prochaine RX.
- Ou : mesurer précisément (avec un témoin et timestamp µs) combien de temps après `uart_write_bytes` les bytes apparaissent réellement sur la ligne.

### A. Notre TX EST émis mais le master rejette nos frames
**Pourquoi :** le master pourrait attendre un break "vrai" (12+ bit-times soit > 625 µs), notre `0x00` à 19200 (~470 µs low) est peut-être insuffisant.

**À tester :**
- Régénérer le break trick baud-switch (9600 7N1) **MAIS** en pré-cachant la config baud pour éviter les ~40 ms de `uart_set_baudrate`. Possible avec accès direct aux registres UART ESP32.
- Ou : utiliser `uart_write_bytes_with_break(port, frame, len, brk_len=20)` qui devrait générer un break trailing matériel propre. Le break suivra notre frame au lieu de la précéder, mais master pourrait quand même le voir comme délimiteur de la frame *suivante* (la nôtre).
- Ou : bit-bang pur sur GPIO17 — désactiver UART temporairement, forcer GPIO low pendant 700 µs, réactiver UART, write_bytes.

### B. Le timing de réponse ne correspond pas à ce qu'attend l'E3
**Pourquoi :** Doc bouni dit que le timing entre scan et reply n'est pas spécifié. Le PIC stephan192 attend 3 ms, raintonr répond immédiatement, HGDO 3 ms aussi. **Mais l'E3 attend peut-être une fenêtre très précise** (genre 1 ms ± 0.5 ms) hors de laquelle notre réponse est ignorée.

**À tester :**
- Mesurer sur le témoin **combien de temps après son scan le master commence à transmettre le broadcast suivant** (donne la taille de la fenêtre disponible)
- Émettre dans cette fenêtre avec différents offsets (500 µs, 1 ms, 2 ms, 5 ms) et chercher celui qui ne provoque PAS de corruption du broadcast suivant

### C. Le protocole E3 diffère subtilement du UAP1 historique
**Pourquoi :** mêmes patterns mais peut-être :
- `slave_type` E3 différent de 0x14 (on a testé 0x10..0x20 mais pas exhaustif)
- Format de scan reply attendu peut-être 6 bytes au lieu de 5 (avec un byte de version/sub-type)
- Adresse esclave non standard (autre que 0x28 et que ce qu'on a essayé)
- Présence d'une étape de **handshake** initial qu'on rate (séquence d'init nécessitant N réponses cohérentes consécutives avant que le master "valide" le slave)

**À tester :**
- Acheter un vrai panneau UAP1 Hörmann et observer ses échanges avec un témoin RS485 → on saurait exactement quoi répondre
- Brute-force tous les `slave_type` de 0x00 à 0xFF avec auto_scan élargi
- Logguer si le master change quoi que ce soit dans son comportement après nos N premières réponses (peut-être qu'il faut N=10 réponses identiques avant qu'il commence le status_request)

### D. Le bus E3 a des sécurités/auth
**Pourquoi :** sur certains modèles récents, Hörmann a introduit du chiffrement/handshake pour empêcher les clones non autorisés. Le E3 est récent.

**À tester :**
- Capturer un long historique avec un vrai UAP1 connecté pour voir s'il y a une séquence d'initialisation au boot (peut-être avec un challenge-response)
- Chercher si des projets DIY Hörmann récents (2023+) mentionnent un changement protocole

### E. Module RS485 inadapté
**Pourquoi :** notre module a DE/RE combinés (un seul pin EN). Quand on TX, notre receiver est désactivé, donc on ne peut pas faire de collision avoidance. De plus, sans contrôle séparé de RE, on ne peut pas écouter pendant un TX pour vérifier que ça passe.

**À tester :**
- Switcher pour un module avec DE et /RE séparés (ex: vrai HW-519 avec DE et /RE différents). Ça permet de mieux gérer la direction et de vérifier l'écho.
- Vérifier la résistance de polarisation du bus (idéalement 120 Ω entre A et B aux deux extrémités physiques)
- Vérifier le niveau électrique de notre TX par oscilloscope (Vdiff doit dépasser ±200 mV proprement)

---

## 6. Outils mis en place pour la prochaine session

### Composant principal
- [components/hormann_hcp1/hormann_hcp1.cpp](components/hormann_hcp1/hormann_hcp1.cpp) : composant ESPHome avec parser HCP1 complet
- [components/hormann_hcp1/__init__.py](components/hormann_hcp1/__init__.py) : config schema avec `slave_addr`, `master_addr`, `slave_type`, `auto_scan`, `de_invert`
- Fonction `tx_diag()` exposée (sans bouton actuellement) pour test manuel TX brut

### Témoin (witness)
- [witness.yaml](witness.yaml) : ESP32-S3 + module RS485 RX-only, dump tout le trafic du bus brut via `uart.debug`
- Câblage : DE/RE → GND, RX → GPIO18, A et B en parallèle sur le bus
- Indispensable pour observer ce qui se passe vraiment sur la ligne quand notre carte principale TX

### YAML carte principale
- [garage-3-test.yaml](garage-3-test.yaml) : config opérationnelle ESP32 + module RS485 sur GPIO4 (DE) / 17 (TX) / 19 (RX)
- Logger en `WARN` pour ne pas ralentir la task bus

### Commandes utiles
```bash
# Build et OTA carte principale
.venv/bin/esphome run garage-3-test.yaml --device garage-3-test.intra.sberard.fr

# Build et OTA témoin
.venv/bin/esphome run witness.yaml --device 192.168.11.10

# Capture parallèle des deux pour comparaison
timeout 30 .venv/bin/esphome logs witness.yaml --device 192.168.11.10 > /tmp/wit.txt &
timeout 30 .venv/bin/esphome logs garage-3-test.yaml --device garage-3-test.intra.sberard.fr > /tmp/main.txt &
wait
```

---

## 4 ter. Résultats session 2026-05-30 — Polarity inversion

### Problème découvert : polarité A/B inversée

**Symptôme :** le master était complètement silencieux du côté des ESP alors qu'il émettait normalement. Le témoin (witness) en mode uart.debug voyait des données structurées mais CRC-invalides.

**Cause :** la polarité A/B de nos modules RS485 (HW-519 cheap) est **inversée** par rapport à ce qu'attend le bus Hörmann. Deux origines possibles (non discriminées, les deux donnent le même symptôme) :
- Le module RS485 a A et B sérigraphiés selon une convention non-EIA485 (fréquent sur les modules chinois)
- Le connecteur du moteur (Pin5/Pin6) utilise une convention opposée à nos modules

**Comment ça se manifeste :**
- Polarité NORMALE (correct) → UART idle = 1 (mark) → frames décodables, CRC valides
- Polarité INVERSÉE (notre cas) :
  - Dans un sens : UART voit idle = 0 = break continu → `uart_debug` ne sort rien, composant custom flush en boucle sans décoder
  - Dans l'autre sens : UART peut framer les bytes MAIS les data bits sont tous complémentés (XOR 0xFF) → CRC toujours invalide → rien décodé

**Solution logicielle confirmée :** `uart_set_line_inverse(port, UART_SIGNAL_RXD_INV)` côté ESP-IDF, ou `inverted: true` sur le rx_pin en ESPHome natif.

**Résultat après correction :** trames HCP1 parfaitement valides sur les deux ESP :
```
00:00:X2:02:02:CRC   ← broadcasts état porte (toutes les ~70 ms)
00:8X:X2:01:80:CRC   ← scans master vers les esclaves
00:28:X2:01:80:CRC   ← scan vers notre adresse UAP1 (0x28)
```

### Hardware confirmé en place
- ✅ Terminaison 120 Ω ajoutée entre A et B (mesure avant : ~3 kΩ → aucune terminaison)
- ✅ VCC module RS485 passé de 3.3 V à 5 V
- ✅ GND commun confirmé entre ESP et moteur

### Ce qui reste bloqué
- ❌ **TX vers le master** : notre TX doit probablement aussi être inversé (`UART_SIGNAL_TXD_INV`) pour que le master décode nos scan-replies. C'est la prochaine étape.
- ❌ Le master ne passe toujours pas au `status_request` — mais avec un RX fonctionnel et un TX potentiellement à corriger, c'est maintenant débloquable.

---

## 4 quater. Résultats session 2026-05-31 — Symptôme idle/break en sens marquage (terminaison sans biais)

### Correction matérielle (vs §4bis)
La puce RS485 n'est **pas un module auto-direction** comme supposé en §4bis : c'est un **SP3485 (TTL, puce 3,3 V) avec une entrée EN** (DE et /RE combinés). C'est un vrai transceiver. EN est piloté par GPIO4 (`de_pin`, `de_invert: false`) : bas = RX, haut = TX. EN est OK (sinon le sens qui décode ne décoderait pas non plus).

### Modèle de polarité affiné (XOR)
Le câblage A/B et `inverted:` (sur le rx_pin du composant uart natif, ex. witness.yaml) sont **deux inversions qui se combinent en XOR**. Seule leur somme `N` compte :

| Câblage | inverted | N = câblage ⊕ inverted | Résultat observé |
|---------|----------|------------------------|------------------|
| A/B (marquage) | false | 0 | **rien** (cf. anomalie ci-dessous) |
| B/A (inversé)  | false | 1 | trames mais CRC invalide |
| B/A (inversé)  | true  | 0 | **trames + CRC valide** ✅ (config witness actuelle) |
| A/B (marquage) | true  | 1 | non testé — prédiction : rien (à confirmer) |

→ La config qui décode aujourd'hui : **câblage inversé (B/A) + `inverted: true`**.

### Ce qui bloque la commande : la polarité du **TX** (pas le « rien »)
Argument décisif, par **asymétrie lecture/écriture**. RX et TX partagent la **même paire différentielle** :
- On **lit** le master (décodage OK en B/A + inversion) → la polarité RX est correctement compensée.
- On n'arrive **pas** à le commander (master muet, jamais de `status_request`).

Si on ne compense que le RX et **pas le TX**, alors à l'émission nos trames partent à l'envers sur le bus → le master les reçoit avec tous les bits complémentés → CRC faux → il nous ignore → jamais de `status_request`. **Cela reproduit exactement le symptôme** (lecture OK / commande morte). C'est un problème **bel et bien au niveau du bus**, et c'est précisément ce que corrige `ab_inverted` (couple `RXD_INV | TXD_INV`, cf. ci-dessous).

**Test qui prouve le TX sans dépendre du master (témoin = oracle) :** le witness décode le master proprement (B/A + `inverted:true`), donc il sait lire la bonne polarité.
1. garage-3 : `ab_inverted: true`, lancer une rafale TX connue (`tx_diag`, ex. `0xAA`).
2. Observer le witness : nos octets sortent-ils **propres** (comme ceux du master) ou en **charabia** ?
   - Propres → TX à la bonne polarité → le master *devrait* pouvoir nous lire → réactiver `auto_scan` et guetter le `status_request`.
   - Charabia → TX encore inversé → creuser.

Cela isole la polarité TX de la logique d'acceptation (inconnue) du master.

### Le « rien » en sens marquage : RÉSOLU — c'était le 5 V (sur-tension)
- **Résolu le 2026-05-31** : repasser le SP3485 en **3,3 V** restaure le signal sur **les deux polarités**. Le 5 V (hors specs, VCC reco ≤ 3,6 V) sortait le récepteur de ses limites et tuait la réception sur une des deux orientations → c'était ça le « rien », **pas** le 120 Ω ni un défaut de biais.
- **Le 120 Ω était bien hors de cause** (confirmé) : présent dans le cas qui décode, et symétrique → ne peut pas créer d'asymétrie d'orientation. Hypothèse « régression terminaison » **abandonnée**.
- À 3,3 V on retrouve le comportement **XOR propre** : les deux orientations cadrent, une seule donne des CRC valides (l'autre = data complémentée). Plus d'asymétrie « rien ».
- ⚠️ **Conséquence pour l'auto-détecteur** : à 3,3 V la mauvaise polarité **cadre** (peu/pas de breaks), donc le déclencheur actuel « beaucoup d'erreurs → basculer » peut ne pas suffire (il verrait « trafic, 0 trame valide, 0 erreur » → branche « pas de trafic »). À durcir : basculer aussi sur « trafic reçu mais 0 trame CRC-valide ». Pour les tests HW, **forcer `ab_inverted: true|false`** plutôt que `auto`.

### Hygiène matérielle
- **SP3485 en 3,3 V** ✅ FAIT — et c'était en fait le **fix du « rien »** (cf. ci-dessus), pas juste de l'hygiène. (Bonus : son RO ne sort plus à ~5 V dans une GPIO ESP 3,3 V non tolérante.)

### Auto-inverseur de polarité — IMPLÉMENTÉ (composant, 2026-05-31)
Le composant possède le port UART en direct (pas de bloc `uart:`), donc l'astuce `inverted: true` d'ESPHome ne s'applique pas. On a implémenté à la place une option **`ab_inverted: auto | true | false`** (défaut `auto`) qui appelle `uart_set_line_inverse()` :
- **Un seul réglage pour RX *et* TX** : un swap A/B inverse les deux sens de la même paire différentielle, donc on les traite ensemble (`RXD_INV | TXD_INV`).
- **`auto`** : au boot, écoute 5 s ; si 0 trame valide + >20 breaks/erreurs de trame → applique l'inversion RX+TX et re-vérifie 5 s. Si ça décode → OK. Sinon → log « pas une simple inversion A/B → vérifier câblage & bias » (= le blocage idle/biais ci-dessus, électrique et **indépendant** : même inversé, si l'idle est en break en sens marquage, le fail-safe reste nécessaire).
- **`true`/`false`** : force, désactive la détection.

**Insight TX (potentiel déblocage de la commande) :** jusqu'ici on ne compensait que le RX (`inverted:true` côté witness), **pas le TX**. En câblage B/A, nos scan-replies partaient donc **inversées** → le master recevait du charabia → il ne répondait jamais au `status_request`. Coupler `TXD_INV` à `RXD_INV` est probablement **le chaînon manquant** pour débloquer la commande. À tester dès que le RX décode proprement (après fix du bias).

---

## 4 quinquies. Session 2026-05-31 (suite) — RX débloqué, crash durci, COLLISION TX identifiée

### Polarité : RÉSOLUE et confirmée
Câblage **sens marquage + `ab_inverted: false`** → **CRC valides à 100 %** sur les deux cartes (garage-3-test GPIO19, witness GPIO18), vérifié trame par trame. Le master nous scanne proprement (`28:82:01:80:06`). RX = parfait. (L'ancien « rien » était le 5 V sur le SP3485 3,3 V, cf. §4quater ; le 120 Ω était innocent.)

### Bug « bus-clamp » (RTS) — la cause du « 0 RX partout »
Le composant assignait `de_pin` (GPIO4) comme **RTS** de l'UART (`uart_set_pin(..., rts=GPIO4, ...)`) tout en restant en DE **manuel** (pas de `UART_MODE_RS485_HALF_DUPLEX`). Sans ce mode, le driver tient RTS **désactivé = HIGH** au repos → EN du SP3485 HIGH → **mode TX permanent** → garage-3 **pilote le bus en continu** → **tous les nœuds voient 0** (witness inclus, d'où le piège : symptôme « global » mais une seule carte coupable). **Fix** : ne plus assigner DE en RTS (`UART_PIN_NO_CHANGE`), DE purement manuel (repos LOW = RX). → bus déclampé, RX OK des deux côtés (282 trames/10 s).

### Crash `bus_task` (core 1) — durci
Une fois le RX actif, le `bus_task` (priorité 23) **inondait les logs DEBUG par octet** (`<<<` + `RX[n]`, gros buffers) → fault core 1 → crash-loop → safe mode (ping OK mais API refusée, port 6053). **Fix** : (1) dump brut passé en `ESP_LOGV` **et** gardé par `#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE` (le `#if` retire aussi la **construction** du hex, pas que le log) ; (2) pile `bus_task` 4096 → 8192. → stable (0 junk côté garage-3, pas de crash).

### Outil : sniffer intégré + junk-log
Option `sniffer: true` → logue à INFO uniquement les trames valides catégorisées (BCAST / SCAN->us / STATUS_REQ->us / ->master), dédupées (compteur ignoré, re-log toutes les 5 s), + heartbeat 10 s (`valid / junk / breaks`). Plus **log des octets « junk »** (rate-limité 500 ms) pour voir les buffers non parsables. Le **witness** tourne ce composant en RX-only (DE/RE→GND) = **oracle TX**.

### 🎯 Bloqueur de commande = COLLISION du TX (vu via junk-log)
Le master nous scanne, garage-3 répond (`TX took ~4,3 ms`), **mais la réponse n'arrive jamais propre** : le witness la voit en **junk**, ex. `80:92:D6:62:00` → nos 2 premiers octets (`80 92`) sont **propres**, puis collision dès l'octet 3 (`D6` superposition, `62` CRC broadcast, `00` sync). Chaque collision **détruit une trame master** (valid 282 → 279, des deux côtés : le master perd notre collision, garage-3 — sourd pendant ses 4,3 ms de TX — rate la trame émise pendant ce temps). → on émet **~1-2 octets trop tard**, par-dessus le broadcast suivant. **Pas la polarité** (RX parfait, même paire). C'est la vieille obs §4 « notre TX corrompt le broadcast », **vue directement**.

**Contributeur de latence** : dans `try_parse_buffered`, `sniff_scan_()` fait un `ESP_LOGI` **bloquant** (« SCAN->us », écriture console ~ms) **avant** `send_frame` → retarde la réponse.

### Prochaines étapes (timing TX)
1. Test isolant : garage-3 `sniffer:false` (bus_task maigre, latence TX minimale) + witness oracle → la réponse atterrit-elle propre ?
2. Si oui → sortir le logging du `bus_task` (ring-buffer drainé dans `loop()`, non bloquant). Si non → instrumenter le délai scan→réponse, viser la fenêtre exacte du master (§5.B ; refs : raintonr = immédiat, stephan192 = ~3 ms).
3. Comprendre `TX took 4,3 ms` pour 6 octets (~3,1 ms de data) — l'octet `0x00` de break en tête + le hold 600 µs après `wait_tx_done`.

---

## 6 bis. Pistes à tester en priorité dès la prochaine session

Par ordre coût/bénéfice :

1. ~~**Alimenter le module RS485 en 5V**~~ ✅ FAIT
2. ~~**Mesure 120 Ω au multimètre**~~ ✅ FAIT — ajouté
3. ~~**`uart_set_line_inverse(RXD_INV | TXD_INV)`** dans le composant~~ ✅ FAIT — implémenté comme option `ab_inverted: auto|true|false` (RX+TX couplés, auto-détection au boot, cf. §4quater). Reste à valider sur le matériel.
4. **Valider la polarité TX via le témoin-oracle** (le witness décode le master clean → nos octets `tx_diag` doivent sortir clean aussi sur le witness), puis **tester si le master répond au scan-reply** avec la correction TX — objectif principal depuis le début.
5. **`master_addr: 0x90`** — jamais testé, valide sur certains modèles (HAP1-HCP-Adapter selon hgdo). À essayer si le scan-reply reste ignoré.
6. **Capture Saleae traces** ([blog.bouni.de](https://blog.bouni.de/posts/2018/hoerrmann-uap1/logic-traces.zip)) — timing exact d'un vrai UAP1.
7. **Module RS485 avec DE et /RE séparés** — pour garder RX actif pendant TX.

---

## 7. Résumé décisionnel

**Pour utiliser le composant tel quel (lecture seule)** : il est utilisable maintenant, l'état porte remonte dans HA. Suffisant pour monitoring/automatisations basées sur l'état.

**Pour aller chercher la commande** :
- Effort estimé : **élevé** (2-5 jours d'investigation supplémentaire)
- Pré-requis idéal : un vrai panneau UAP1 Hörmann pour comparer les frames émises (~50€ d'occasion)
- Outils utiles : analyseur logique Saleae clone (~25€), oscilloscope si possible
- Sans ces outils : tester systématiquement les pistes A/B/C ci-dessus

**Workaround court terme** : si vous voulez piloter la porte sans attendre le fix protocole, brancher un module avec contact sec en parallèle des boutons physiques de l'opérateur (impulse simple). Pas élégant mais marche tout de suite.
