# nix-search

Проверяет, есть ли пакет в nixpkgs, который реально используется системой.

Утилита написана на C11. Python не используется.

## Использование

```sh
nix-search hello
nix-search firefox
```

Вывод:

- `есть` — пакет существует в nixpkgs;
- `такого пакета нет` — пакета нет.

Коды возврата:

- `0` — пакет есть;
- `1` — пакета нет;
- `2` — ошибка (не удалось определить канал или запустить `nix`).

## Как выбирается nixpkgs

Канал определяется автоматически:

1. локальный `<nixpkgs>` из `NIX_PATH`;
2. `$HOME/.nix-defexpr/channels/nixpkgs`;
3. `nix registry list`;
4. `nix-channel --list`;
5. `/etc/os-release` на NixOS;
6. `nixos-version`;
7. если ничего не найдено — `nixos-unstable`.

Если локальный nixpkgs найден, проверка идёт именно в нём. Для NixOS 26.05
это будет 26.05, для unstable — unstable.

Проверка выполняется через:

```sh
nix eval --raw <nixpkgs>#legacyPackages.<system>.<package>.outPath
```

Если в `legacyPackages` пакета нет, дополнительно проверяется
`packages.<system>.<package>`.

## Сборка

```sh
make
./nix-search hello
```

Установка:

```sh
sudo make install
```

Или через Nix:

```sh
nix run github:zyzya0/nix-search -- hello
nix profile install github:zyzya0/nix-search
```

## Структура

```
src/main.c      точка входа и проверка пакета
src/channel.c   автоопределение канала nixpkgs
src/util.c      запуск процессов, строки, файлы
src/*.h         заголовочные файлы
tests/          C-тест определения каналов
```

## Лицензия

MIT. См. [LICENSE](LICENSE).
