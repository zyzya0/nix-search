# nix-search (Rust)

Показывает информацию о пакете в nixpkgs, который реально используется
системой: имя, версию, описание и зависимости.

Это ветка `rust` — переписывание C-версии из ветки `main` на Rust.
Я этой веткой не пользуюсь и вам не советую, она пока-что не очень стабильна.


Все тесты проводились на NixOS.

## Использование

```sh
nix-search hello
nix-search firefox
nix-search python3Packages.requests
```

Пример вывода:

```text
Пакет:       hello
Полное имя:  hello-2.12.3
Версия:      2.12.3
Описание:    Program that produces a familiar, friendly greeting

Зависимости:
  build: (нет)
  native (1):
    - version-check-hook
  propagated: (нет)
```

Если пакета нет:

```text
такого пакета нет
```

Коды возврата:

- `0` — пакет найден;
- `1` — пакета нет;
- `2` — ошибка (некорректное имя, не удалось определить канал или запустить `nix`).

## Как выбирается nixpkgs

Канал определяется автоматически:

1. локальный `<nixpkgs>` из `NIX_PATH`;
2. `$HOME/.nix-defexpr/channels/nixpkgs`;
3. `nix registry list`;
4. `nix-channel --list`;
5. `/etc/os-release` на NixOS;
6. `nixos-version`;
7. если ничего не найдено — `nixos-unstable`.

Если локальный nixpkgs найден, данные берутся именно из него. Для NixOS
26.05 это будет 26.05, для unstable — unstable.

## Как получаются данные

Всё делается одним вызовом `nix eval`:

- пакет ищется сначала в `legacyPackages.<system>`, затем в
  `packages.<system>`;
- поддерживаются вложенные атрибуты (`python3Packages.requests`);
- возвращаются `pname`, `name`, `version`, `meta.description`,
  `buildInputs`, `nativeBuildInputs` и `propagatedBuildInputs`.

Имя пакета **не подставляется в Nix-выражение**. Оно передаётся через
переменные окружения и читается через `builtins.getEnv`, поэтому кавычки,
точки с запятой и прочий ввод не могут инжектировать Nix-код. Дополнительно
имя проверяется: запрещены пустые части между точками, пробелы и
управляющие символы.

## Сборка

```sh
cargo build --release
./target/release/nix-search hello
```

Тесты:

```sh
cargo test
```

Или через Nix:

```sh
nix build
./result/bin/nix-search hello
nix run github:zyzya0/nix-search/rust -- hello
```

## Структура

```text
src/main.rs      точка входа и форматирование вывода
src/channel.rs   автоопределение канала nixpkgs
src/package.rs   поиск пакета, метаданные и JSON
Cargo.toml       зависимости (serde_json)
Cargo.lock       зафиксированные версии зависимостей
```

## Лицензия

MIT. См. [LICENSE](LICENSE).
