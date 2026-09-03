# Guide de contribution — firmware

Comment installer l'environnement de dev, builder, flasher et pousser du code sur ce repo.
Pour comprendre le fonctionnement interne du firmware, voir [doc.md](./doc.md).

## Environnement de développement et commandes du terminal

### Installation

- Installer l'extension PlatformIO sur vscode :  [PlatformIO](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- ou chercher "PlatformIO IDE" dans les extensions

### Commande dans le terminal

```bash
pio test -e native        # pour les tests logique (fonctions, ...), se fait en local
pio run -e esp32-s3       # build le firmware (en local)
pio run -e esp32-s3 -t upload             # build et flash la carte
pio run -e esp32-s3 -t upload -t monitor  # flash + serial 115200
pio run -e esp32-s3 -t size               # taille flash/RAM
pio run -e esp32-s3-ci                    # build "sans capteurs"
pio device monitor --baud 115200          # voir les logs
pio check -e esp32-s3 --skip-packages     # cppcheck
pio run -t clean                          # vide .pio/build
pio device list
```

### Adaptation des capteurs
Il y a des environnement dans le fichier platformio.ini
- `esp32-s3` environnement de dev pour les capteurs, il faut les activer !
- `esp32-s3-ci` n'active presque rien -> il permet de tester le bluetouth quand même !
- `native` ne compile aucun code matériel

Pour prendre en compte un capteur, il faut faire #ifdef
```CPP
#ifdef HAS_IMU
  #include "imu.h"
  IMU imu;
#endif

void setup() {
#ifdef HAS_BLUETOOTH
    bluetooth_init();
#endif
#ifdef HAS_IMU
    imu.begin();
#endif
}

void loop() {
#ifdef HAS_IMU
    float accel = imu.readAccel();
#endif
}
```

### Ajouter une librairie

Exemple concret : ajouter l'oxymètre SEN0344 (lib `DFRobot_BloodOxygen_S`).

**1. Déclarer la lib — dans le bon env**

Dans `platformio.ini`, sous `[env:esp32-s3]`

```ini
[env:esp32-s3]
board = seeed_xiao_esp32s3
lib_deps =
    dfrobot/DFRobot_BloodOxygen_S
build_flags =
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D HAS_BLUETOOTH
    -D HAS_IMU
    -D HAS_OXYGEN      ; <- ton nouveau flag
```

Pourquoi pas sous `[env]` : `[env]` s'applique aussi à `native`, qui compile sur
PC.

**2. Protéger le code par un flag**

Tout ce qui touche le capteur va derrière `#ifdef` — sinon la CI (`esp32-s3-ci`,
sans capteurs) et le esp32 qui n'a pas le module ne compilent plus :

```cpp
#ifdef HAS_OXYGEN
  #include "sensors/oximeter.h"
  Oximeter oxy;
#endif

void setup() {
#ifdef HAS_OXYGEN
    oxy.begin();
#endif
}
```

**3. Vérifier avant de push — les 3 envs**

```bash
pio test -e native        # le code portable compile toujours
pio run -e esp32-s3-ci    # le build "sans capteurs" compile toujours
pio run -e esp32-s3       # ton build avec le capteur compile
```

Si un des trois casse, c'est que la lib ou le `#include` a fuité hors du `#ifdef`.

**Cas particulier : lib de logique pure (pas de matériel)**

Si le code n'utilise pas Arduino, ajoute aussi le fichier au filtre natif pour
qu'il soit testé par la CI :

```ini
[env:native]
build_src_filter = +<message.cpp> +<logic/mon_fichier.cpp>
```

### Secrets
Les logins sont dans le fichier `include/secrets.h`. Ne pas le commiter !

### workflow pour un push

```bash
pio test -e native      # 1. les tests logiques passent
pio run -e esp32-s3     # 2. le firmware compile bien
pio run -e esp32-s3-ci  # 3.le build "sans capteurs" compile toujours
<TEST MANUEL>           # 4. test sur carte de bout-en-bout
git push                # 5. la CI prend le relais (build + test sur carte)
```

### Environnement de dev sans capteurs

Utilise l'env esp32-s3-ci qui ne prends pas en compte les capteurs.

```
pio test -e native      # pour les tests logique (fonctions, ...), se fait en local
pio run -e esp32-s3-ci  # build le firmware (en local)
pio run -e esp32-s3-ci -t upload  # build et flash la carte
pio device monitor --baud 115200  # voir les logs
```
