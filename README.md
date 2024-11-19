# cgotpl

![Build Status](https://img.shields.io/github/actions/workflow/status/Nuckal777/cgotpl/checks.yaml?branch=master)

C99 [golang-style](https://pkg.go.dev/text/template) template engine

**Be careful with untrusted input!**
## Usage

cgotpl comes with a simple CLI.
```sh
cgotpl [TEMPLATE] [DATA]
```
Here `TEMPLATE` refers to a [golang-style](https://pkg.go.dev/text/template) template string and `DATA` to a serialized [JSON](https://www.rfc-editor.org/rfc/rfc8259) string.
For instance:
```sh
cgotpl '{{ range . -}} {{.}} {{- end }}' '["h", "e", "ll", "o"]'
```
Will print `hello` on stdout.

## API

There's a single central function defined in `template.h`:
```c
// tpl is a pointer to a template string from which up to n bytes are read.
// dot is the inital dot value. out will be filled with the result of templating
// and needs to be freed by the caller. Returns 0 on success.
int template_eval(const char* tpl, size_t n, json_value* dot, char** out);
```
An initalized `json_value` can be obtained from:
```c
// Consumes an abitrary amount of bytes from st to parse a single JSON value
// into val. Returns 0 on success.
int json_parse(stream* st, json_value* val);
```
A `json_value` needs to be freed with:
```c
void json_value_free(json_value* val);
```
A `stream` can be created with:
```c
// Opens stream backed by data up to len bytes.
void stream_open_memory(stream* stream, const void* data, size_t len);
// Opens stream on the file referenced by filename. Returns 0 on success.
int stream_open_file(stream* stream, const char* filename);
```
A `stream` needs be closed with:
```c
// Closes stream. Returns 0 on success.
int stream_close(stream* stream);
```
See [`cli/main.c`](cli/main.c) for a complete example.

## Building

[cmake](https://cmake.org/) version 3.16 or greater is required.
Initialize cmake with:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```
The following will produce the cgotpl CLI in `build/cli/cgotpl`:
```sh
cmake --build build --target cli
```
The library can be build with:
```sh
cmake --build build --target cgotpl
```
For development a `check` (requires a go compiler) and `fuzz` (requires `CC=clang`) target exist.

## Features

Currently, basic control-flow and data access is implemented.

| Feature                                       | State                                              |
| --------------------------------------------- | -------------------------------------------------- |
| Trim whitespace `{{-` and `-}}`               | :white_check_mark:                                 |
| `{{/* comments */}}`                          | :x:                                                |
| Default Textual Representation `{{pipeline}}` | :white_check_mark:                                 |
| Literals                                      | :construction: (Some escape sequences are missing) |
| `{{if pipeline}} T1 {{end}}`                  | :white_check_mark:                                 |
| `{{if pipeline}} T1 {{else}} T0 {{end}}`      | :white_check_mark:                                 |
| `{{if p}} T1 {{else if p}} T0 {{end}}`        | :x:                                                |
| `{{range pipeline}} T1 {{end}}`               | :white_check_mark:                                 |
| `{{range pipeline}} T1 {{else}} T0 {{end}}`   | :white_check_mark:                                 |
| `{{break}}`                                   | :x:                                                |
| `{{continue}}`                                | :x:                                                |
| `{{define}}`                                  | :x:                                                |
| `{{template "name" pipeline}}`                | :x:                                                |
| `{{block "name" pipeline}} T1 {{end}}`        | :x:                                                |
| `{{with pipeline}} T1 {{end}}`                | :x:                                                |
| Field access `.a.b.c`                         | :white_check_mark:                                 |
| Variables                                     | :x:                                                |
| Functions (e.g. `printf`, `not`, `and`, ...)  | :x:                                                |

The c and go standard library may disagree on certain formatting (`printf`) corner-cases.

## Why?

I wanted to make something in C.