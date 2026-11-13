# Water Click

[English](README.md)

Короткая водяная волна от нажатия левой кнопки мыши в Hyprland. Волна
преломляет изображение вокруг курсора, расходится до 80 логических пикселей
и затухает за 250 мс. Новый клик заменяет предыдущую волну. Приложения,
панель и рабочий стол получают нажатия как обычно.

![Три короткие волны от левого клика](docs/water-click.gif)

[Запись терминала](docs/water-click-desktop.gif) показывает тот же короткий
профиль из прототипа HyprRipple в рабочем сеансе Omarchy.

При блокировке сеанса или отключении анимаций Hyprland эффект приостанавливается.
В полноэкранном режиме с прямым выводом (direct scanout) волна может не появиться.
Панель состояния следует языку Omarchy Shell и доступна на русском и английском.

## Установка

Для сборки нужны заголовки и библиотеки той же сборки Hyprland, что запущена в
сеансе, а также `make`, компилятор с поддержкой C++23, `pkg-config` и файлы
разработки pixman, libdrm и OpenGL ES. Модуль поддерживает Hyprland 0.56 и 0.57.
Проверено на 0.56.2 и 0.57.2. Панель состояния работает в Omarchy Shell 4.5.

```sh
omarchy plugin add https://github.com/i4w7w4a/omarchy-water-click --enable
cd "$HOME/.config/omarchy/plugins/io.github.i4w7w4a.water-click" &&
  make &&
  install -Dm755 build/waterclick.so "$HOME/.local/lib/waterclick/waterclick.so" &&
  hyprctl plugin load "$HOME/.local/lib/waterclick/waterclick.so" &&
  hyprctl waterclick get
```

Открыть панель можно командой `omarchy-shell shell toggle io.github.i4w7w4a.water-click`.
Если в сеансе уже работает другой эффект клика, отключите в нём реакцию на
левую кнопку, чтобы видеть одну волну.

Для автозагрузки добавьте в пользовательский Lua-конфиг Hyprland:

```lua
hl.plugin.load(os.getenv("HOME") .. "/.local/lib/waterclick/waterclick.so")
```

После обновления Hyprland пересоберите модуль с подходящими заголовками.
Выполните `make clean` и `make` перед установкой новой библиотеки.
Если старая библиотека загружена, выгрузите её перед заменой и загрузите
новую после установки.

## Удаление

Удалите строку автозагрузки, если добавляли её. Выгрузите модуль, если он
загружен, и удалите библиотеку и панель.

```sh
hyprctl plugin unload "$HOME/.local/lib/waterclick/waterclick.so"
rm -f "$HOME/.local/lib/waterclick/waterclick.so"
omarchy plugin remove io.github.i4w7w4a.water-click
```

Для отрисовки волны использован и адаптирован код
[HyprRipple](https://github.com/jgarza9788/hyprripple), автор jgarza. Water Click
распространяется по [лицензии MIT](LICENSE).
