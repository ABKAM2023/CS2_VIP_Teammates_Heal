**Для работы модуля необходимо обновить до последней версии [Utils](https://github.com/Pisex/cs2-menus/releases).**

**[VIP] Teammates Heal** — добавляет возможность лечения союзников при попадании по ним для VIP-игроков. 

[Видео-демонстрация](https://www.youtube.com/watch?v=SjS9edeV5zQ)

В `groups.ini` добавьте следующее:
```ini
  "heal_teammates" "50" // Процент от нанесенного урона, который восстанавливается союзнику
```

В файл `vip.phrases.txt` добавьте следующее:
```
    "heal_teammates"
    {
        "en"    "Heal Teammates"
        "ru"    "Лечение союзников"
    }
```

Требования: [VIP CORE](https://csdevs.net/resources/vip-core.511/)

