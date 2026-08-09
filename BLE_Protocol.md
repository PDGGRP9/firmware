# Protocole BLE — Bracelet connecté BRASCO

**Projet** : Bracelet connecté (rythme cardiaque, oxymétrie, compteur de pas)
**Firmware** : XIAO ESP32S3
**Dernière mise à jour** : 09/08/2026
**Statut** : En développement — sujet à évolution mineure tant que l'app mobile n'est pas figée

---

## 1. Identification du device

| Paramètre | Valeur |
|---|---|
| Nom BLE (advertising) | `BRASCO-00` |
| Device ID (dans le JSON) | `BRASCO-00` |
| Passkey d'appairage (bonding) | `123456` (statique, pairing sécurisé) |

⚠️ Le nom BLE `BRASCO-00` est temporaire pour la phase de dev. Il faudra le changer avant la démo finale.

---

## 2. Service et caractéristique

| Élément | UUID |
|---|---|
| **Service** | `146ef449-0083-438a-9af6-5be5bb541e2c` |
| **Caractéristique Data** | `146ef450-0083-438a-9af6-5be5bb541e2c` |

**Propriétés de la caractéristique** : `READ`, `NOTIFY`

---

## 3. Format des données (JSON)

Chaque notification envoyée par le bracelet contient un objet JSON unique avec **toutes** les métriques.

### Structure

```json
{
  "device_id": "BRASCO-00",
  "hr": 72,
  "spo2": 98,
  "steps": 1543
}