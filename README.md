# ESPHome Hörmann Garage Door Controller

Composant ESPHome pour contrôler les portes de garage Hörmann via le protocole HCP1.

⚠️ **UTILISEZ À VOS PROPRES RISQUES !** Ce projet interagit directement avec le moteur de votre porte de garage.

## Matériel requis

### Composants
- **ESP32** (ESP32-DevKit, NodeMCU-32S, etc.)
- **Module RS485 HW-519** (basé sur MAX485)
- Câbles Dupont
- Alimentation 5V pour l'ESP32

### Connexion au moteur Hörmann

Le moteur Hörmann dispose d'un connecteur avec le brochage suivant :

| Pin | Fonction |
|-----|----------|
| 1 | ??? |
| 2 | 24VDC |
| 3 | GND |
| 4 | ??? |
| 5 | RS485 DATA- (B) |
| 6 | RS485 DATA+ (A) |

## Câblage

Il existe deux types de modules HW-519 :
- **Version 4 pins** (VCC, GND, TX, RX) - Auto-direction, plus simple
- **Version 6+ pins** (avec DE/RE) - Contrôle manuel de direction

### Option 1 : Module HW-519 Auto-direction (4 pins - Recommandé)

Si votre module n'a que **VCC, GND, TX, RX**, il gère automatiquement la direction RS485.

```
ESP32                HW-519              Hörmann Motor
┌─────────┐         ┌───────┐           ┌─────────────┐
│    3.3V ├─────────┤ VCC   │           │             │
│     GND ├─────────┤ GND   │           │  Pin 3 (GND)│
│  GPIO17 ├─────────┤ RX    │     A ────┤  Pin 6 (A+) │
│  GPIO16 ├─────────┤ TX    │     B ────┤  Pin 5 (B-) │
└─────────┘         └───────┘           └─────────────┘
```

> ⚠️ **Attention au croisement** : TX de l'ESP32 (GPIO17) → RX du module, et TX du module → RX de l'ESP32 (GPIO16)

**Configuration YAML :**
```yaml
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 19200

hormann_hcp1:
  id: hormann
  # Pas besoin de de_pin/re_pin avec l'auto-direction
```

### Option 2 : Module HW-519 avec contrôle DE/RE (6+ pins)

Si votre module a les pins **DI, RO, DE, RE**, vous devez gérer la direction manuellement.

```
ESP32                HW-519 (MAX485)     Hörmann Motor
┌─────────┐         ┌─────────────┐     ┌─────────────┐
│    3.3V ├─────────┤ VCC         │     │             │
│     GND ├─────────┤ GND         │     │  Pin 3 (GND)│
│  GPIO17 ├─────────┤ DI          │     │             │
│  GPIO16 ├─────────┤ RO          │  A ─┤  Pin 6 (A+) │
│   GPIO4 ├────┬────┤ DE          │  B ─┤  Pin 5 (B-) │
│         │    └────┤ RE          │     │             │
└─────────┘         └─────────────┘     └─────────────┘
```

> 💡 **Astuce** : Vous pouvez ponter DE et RE ensemble et utiliser un seul GPIO, ou utiliser deux GPIO séparés.

**Configuration YAML :**
```yaml
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 19200

hormann_hcp1:
  id: hormann
  de_pin: GPIO4
  re_pin: GPIO5  # Ou même pin que de_pin si pontés
```

### Connexion au moteur Hörmann

Le moteur Hörmann dispose d'un connecteur avec le brochage suivant :

| Pin | Fonction |
|-----|----------|
| 1 | +24VDC (sortie) |
| 2 | +24VDC (sortie) |
| 3 | GND |
| 4 | Réservé |
| 5 | RS485 DATA- (B) |
| 6 | RS485 DATA+ (A) |

### Schéma complet

```
                                                      ┌─────────────────────┐
┌─────────────────┐         ┌─────────────────┐      │   Hörmann Motor     │
│     ESP32       │         │     HW-519      │      │                     │
│                 │         │                 │      │ ┌─────────────────┐ │
│  3.3V ──────────┼─────────┤ VCC             │      │ │ 1 │ +24VDC      │ │
│                 │         │                 │      │ │ 2 │ +24VDC      │ │
│  GND ───────────┼────┬────┤ GND         GND ├──────┼─│ 3 │ GND         │ │
│                 │    │    │                 │      │ │ 4 │ Réservé     │ │
│  GPIO17 (TX) ───┼────┼────┤ RX/DI           │      │ │ 5 │ DATA- (B) ──┼─┼─── B
│                 │    │    │                 │      │ │ 6 │ DATA+ (A) ──┼─┼─── A
│  GPIO16 (RX) ───┼────┼────┤ TX/RO       A ──┼──────┼─┘                 │ │
│                 │    │    │             B ──┼──────┼───────────────────┘ │
│  GPIO4* ────────┼────┼────┤ DE*             │      │                     │
│  GPIO5* ────────┼────┼────┤ RE*             │      └─────────────────────┘
│                 │    │    │                 │
└─────────────────┘    │    └─────────────────┘
                       │
                       └── Masse commune importante !

* Uniquement pour modules avec contrôle DE/RE
```

## Installation

### Option 1 : Depuis GitHub (Recommandé)

Ajoutez simplement le composant externe dans votre fichier YAML :

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/AntorFr/esphome-Hormann
      ref: main  # ou une version spécifique comme v1.0.0
    components: [ hormann_hcp1 ]
```

### Option 2 : Installation locale

#### 1. Cloner le repository

```bash
git clone https://github.com/AntorFr/esphome-Hormann.git
cd esphome-Hormann
```

#### 2. Utiliser le chemin local

```yaml
external_components:
  - source:
      type: local
      path: components
```

### Configurer les secrets

```bash
cp secrets.yaml.example secrets.yaml
# Éditer secrets.yaml avec vos informations WiFi
```

### Compiler et flasher

```bash
# Avec ESPHome CLI
esphome run example_hcp1.yaml

# Ou avec ESPHome Dashboard
# Ajouter le fichier example_hcp1.yaml dans le dashboard
```

## Configuration YAML

### Configuration minimale (depuis GitHub)

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/AntorFr/esphome-Hormann
    components: [ hormann_hcp1 ]

uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 19200

hormann_hcp1:
  id: hormann
  # de_pin et re_pin optionnels si module auto-direction

cover:
  - platform: hormann_hcp1
    name: "Garage Door"
    hormann_hcp1_id: hormann
```

### Configuration avec contrôle DE/RE

```yaml
hormann_hcp1:
  id: hormann
  de_pin: GPIO4
  re_pin: GPIO5  # Peut être le même que de_pin si pontés
```

### Configuration complète

Voir [example_hcp1.yaml](example_hcp1.yaml) pour un exemple complet avec tous les capteurs et boutons.

## Entités Home Assistant

Une fois configuré, les entités suivantes seront disponibles dans Home Assistant :

### Cover (Porte de garage)
- **Garage Door** - Contrôle principal de la porte (ouvrir/fermer/stop)

### Light (Éclairage)
- **Garage Light** - Contrôle de l'éclairage du garage

### Binary Sensors (Capteurs binaires)
- **Light Status** - État de l'éclairage
- **Error** - Indicateur d'erreur
- **Venting Position** - Position ventilation (porte entre-ouverte)
- **Pre-warning** - Pré-avertissement avant mouvement

### Buttons (Boutons)
- **Impulse** - Impulsion (comme la télécommande)
- **Venting Position** - Mettre en position ventilation
- **Emergency Stop** - Arrêt d'urgence

## Protocole HCP1

Le composant émule un module UAP1 Hörmann pour communiquer avec le moteur.

### Paramètres de communication
- **Baudrate**: 19200
- **Bits de données**: 8
- **Parité**: Aucune
- **Bits de stop**: 1
- **Protocole**: RS485 half-duplex

### Adresses
- `0x00` - Broadcast
- `0x80` - Master (moteur)
- `0x28` - UAP1 (notre émulateur)

### Commandes supportées
| Action | Description |
|--------|-------------|
| Open | Ouvrir la porte |
| Close | Fermer la porte |
| Stop | Arrêter le mouvement |
| Impulse | Impulsion (inverse la direction) |
| Venting | Position ventilation |
| Toggle Light | Basculer l'éclairage |
| Emergency Stop | Arrêt d'urgence |

## Dépannage

### La porte ne répond pas
1. Vérifier le câblage RS485 (A/B peuvent être inversés)
2. Vérifier que le GND est connecté
3. Activer le mode DEBUG dans les logs ESPHome

### Erreur 7 sur le moteur
L'erreur 7 indique que le moteur ne reçoit pas de réponse du "slave" (UAP1). Vérifiez :
- Le câblage
- La configuration du port UART
- Les pins DE/RE

### Logs de debug

Activez les logs détaillés :

```yaml
logger:
  level: DEBUG
  logs:
    hormann_hcp1: DEBUG
```

## Moteurs compatibles

Ce composant a été développé pour les moteurs Hörmann utilisant le protocole HCP1 :
- Supramatic E3
- Supramatic E4
- ProMatic 4
- Et autres modèles avec bus UAP1

## Références

- [hoermann_door par stephan192](https://github.com/stephan192/hoermann_door) - Projet de référence
- [Blog Bouni - Hörmann UAP1](https://blog.bouni.de/posts/2018/hoerrmann-uap1/) - Reverse engineering du protocole

## License

MIT License - Voir [LICENSE](LICENSE)

## Contribution

Les contributions sont les bienvenues ! N'hésitez pas à :
- Signaler des bugs
- Proposer des améliorations
- Tester avec différents modèles de moteurs Hörmann

## TODO

- [ ] Support du protocole HCP2
- [x] ~~Position précise de la porte~~ - Non supporté par HCP1 (états binaires uniquement)
- [ ] Intégration avec d'autres plateformes domotiques
