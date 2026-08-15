# Подготовка отчётных материалов

## Преобразования при рефакторинге

Реализованы три преобразования:

1. Для базового класса с наследниками невиртуальный деструктор получает `virtual`.
2. Метод, который переопределяет виртуальный метод базового класса, получает `override`.
3. В `range-for` для константной переменной нефундаментального типа добавляется ссылка: `const T value` превращается в `const T& value`.

При добавлении `override` сохраняются суффиксы объявления метода, в том числе `const`, `noexcept`, `&`, `&&`, `final`, а также комментарии между элементами объявления.

## Логирование изменений

Каждое реально применённое изменение можно записать в отдельный лог-файл:

```bash
./build/refactor_tool --log-file reports/refactor.log source.cpp -- -std=c++23
```

Если `--log-file` не указан, по умолчанию используется `refactor.log`.

В логе сохраняются тип преобразования, имя исходного файла, позиция и имя изменённой сущности. Это позволяет сопоставить автоматическое изменение с конкретным местом в коде.

## Сборка проекта с AddressSanitizer

В корневом `CMakeLists.txt` предусмотрена опция `ENABLE_ASAN`.

Ручная сборка:

```bash
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++-20 \
    -DCT_Clang_INSTALL_DIR=/usr/lib/llvm-20 \
    -DENABLE_ASAN=ON
cmake --build build-asan --parallel
```
Запуск тестов инструментированной сборки:

```bash
ASAN_OPTIONS=detect_leaks=1 \
ctest --test-dir build-asan --output-on-failure
```

В VS Code используются задачи:

- `ASAN: Build Debug` — конфигурирует и собирает `build-asan`;
- `ASAN: Test` — сначала собирает проект с ASAN, затем запускает тесты.

Успешный прогон означает, что тесты завершились без сообщений `AddressSanitizer`/`LeakSanitizer` и `ctest` сообщает о прохождении всех тестов.

## Отчёт ASAN до и после рефакторинга

Для исследования используется файл `tests/tests_data/leak_example.cpp`.

Скрипт `scripts/asan_report.sh`:

1. создаёт временную копию исходного примера;
2. компилирует и запускает вариант **до** рефакторинга с ASAN;
3. применяет `refactor_tool` ко второй копии;
4. компилирует и запускает вариант **после** рефакторинга;
5. сохраняет вывод в каталог `reports`.

Запуск вручную:

```bash
./scripts/asan_report.sh ./build/refactor_tool reports
```

Или через VS Code:

```text
Terminal → Run Task... → Reports: ASAN
```

Создаются файлы:

```text
reports/
├── asan_before.txt
├── asan_after.txt
├── asan_summary.txt
└── asan_refactor.log
```

### Контрольные точки в отчётных документах

В `asan_before.txt` ожидается сообщение LeakSanitizer об утечке, вызванной удалением производного объекта через указатель на базовый класс с невиртуальным деструктором.

В `asan_summary.txt` до исправления ожидается ненулевой код возврата, а после исправления — `0`:

```text
before exit code: 1
after exit code:  0
```

Критерий проверки - наличие ошибки до рефакторинга и её отсутствие после него.

В `asan_after.txt` после добавления `virtual` не должно быть сообщений `ERROR: AddressSanitizer` или `ERROR: LeakSanitizer`.

`asan_refactor.log` подтверждает, что изменение было сделано именно утилитой.

## Профилирование с perf

Для сравнения производительности используется `tests/tests_data/perf_example.cpp`.

До рефакторинга цикл вида:

```cpp
for (const auto obj : vec) {
    // ...
}
```

создаёт копию объекта на каждой итерации. После работы утилиты цикл становится:

```cpp
for (const auto& obj : vec) {
    // ...
}
```

и лишнее копирование устраняется.

### Получение отчётов

Запуск вручную:

```bash
sudo ./scripts/perf_report.sh ./build/refactor_tool reports
```

Или через VS Code:

```text
Terminal → Run Task... → Reports: perf
```

Скрипт создаёт:

```text
reports/
├── perf_before.txt
├── perf_after.txt
├── perf_before_report.txt
├── perf_after_report.txt
└── perf_refactor.log
```

`perf stat` выполняется несколько раз, критические точки: среднее `seconds time elapsed`, `cycles` и `instructions`.

Ожидаемый результат: после добавления `const &` время выполнения и объём работы, связанной с копированием объектов, уменьшаются.

Файлы `perf_before_report.txt` и `perf_after_report.txt` содержат текстовый профиль `perf report --stdio` и прилагаются вместе с результатами `perf stat`.

## Одновременное создание отчётов

В VS Code есть составная задача:

```text
Reports: ASAN + perf
```

Она последовательно запускает ASAN- и perf-сценарии и складывает все результаты в `reports/`.

Если `perf` недоступен из-за ОС или системных ограничений, запускайте отдельно `Reports: ASAN`, а perf выполняйте в Linux-окружении, где разрешён доступ к performance counters.

## Перечень отчётных материалов

Перечень отчётных материалов содержится в каталоге `reports`:

```text
asan_before.txt
asan_after.txt
asan_summary.txt
asan_refactor.log
perf_before.txt
perf_after.txt
perf_before_report.txt
perf_after_report.txt
perf_refactor.log
```
## VS Code tasks

Полный набор добавленных задач:

```text
GCC: Build Debug
GCC: Build Release
ASAN: Build Debug
Reports: ASAN
Reports: perf
Reports: ASAN + perf
Clean: build and reports
```

# Ручное формирование отчётных документов при проблемах с получением прав доступа для perf в контейнере
## Отчёты
```bash
sudo perf stat -r 5 ./perf_before 2> reports/perf_before.txt
sudo perf stat -r 5 ./perf_after 2> reports/perf_after.txt
```

## Профиль
```bash
sudo perf record -g -o reports/perf_before.data ./perf_before
sudo perf report --stdio -i reports/perf_before.data > reports/perf_before_report.txt

sudo perf record -g -o reports/perf_after.data ./perf_after
sudo perf report --stdio -i reports/perf_after.data > reports/perf_after_report.txt
```