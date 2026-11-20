# Ziomki

Serwis społecznościowy w czystym C11, zbudowany według DDD. Ma dwa interfejsy na tym samym rdzeniu: konsolę i stronę w przeglądarce.

## Uruchomienie

Windows (clang):

```powershell
.\build.ps1
.\ziomki-web.exe 8080
```

Potem otwierasz http://localhost:8080. Wersja konsolowa to `.\ziomki.exe`.

Linux i macOS:

```sh
make && make test
./ziomki-web 8080
```

Argumenty: `ziomki-web [port] [plik-bazy] [plik-dziennika]`. Domyślnie zapisuje dane do `ziomki.db`, a zdarzenia do `ziomki-zdarzenia.log`.

## Co umie

- rejestracja i logowanie po nicku (bez haseł),
- zaproszenia do znajomych; zaproszenie wysłane w obie strony od razu tworzy znajomość,
- wpisy do 280 znaków, które widzą tylko autor i jego znajomi,
- polubienia i komentarze do 140 znaków,
- podpowiedzi „Osoby, które możesz znać” i wyszukiwarka.

## Architektura

| Warstwa | Katalog | Zawartość |
|---|---|---|
| Domena | `src/domain` | value objects, agregaty `User` i `Post`, usługa znajomości, reguła widoczności, zdarzenia, porty |
| Aplikacja | `src/application` | `SocialService`, czyli przypadki użycia |
| Infrastruktura | `src/infrastructure` | repozytoria w pamięci z zapisem do pliku, zegar, dziennik zdarzeń |
| Interfejs | `src/interface` | konsola (`cli.c`) i serwer HTTP z HTML-em (`web.c`) |

Porty to struktury wskaźników na funkcje, a adaptery podpina `main.c` albo `main_web.c`. Serwer HTTP jest jednowątkowy i słucha tylko na `127.0.0.1`.

## Testy

`build.ps1` albo `make test` uruchamia `tests/test_ziomki.c` (64 asercje).
