# cgotpl

![Build Status](https://img.shields.io/github/actions/workflow/status/Nuckal777/cgotpl/checks.yaml?branch=master)

C99 [golang-style](https://pkg.go.dev/text/template) template engine

**Be careful with untrusted input!**
## Usage

cgotpl comes with a simple CLI.
```sh
cgotpl ([TEMPLATE] | -f [FILENAME]) [DATA]
```
Here `TEMPLATE` refers to a [golang-style](https://pkg.go.dev/text/template) template string and `DATA` to a serialized [JSON](https://www.rfc-editor.org/rfc/rfc8259) string.
The `-f` flag can be used to read the template from a file.
For instance:
```sh
cgotpl '{{ range . -}} {{.}} {{- end }}' '["h", "e", "ll", "o"]'
```
Will print `hello` on stdout.

## API

There are two central function defined in `template.h`:
```c
// in is a pointer to a stream, which may be read to the end. dot is
// the inital dot value. out will be filled with the result of templating
// and needs to be freed by the caller. Returns 0 on success.
int template_eval_stream(stream* in, json_value* dot, char** out);

// tpl is a pointer to a template string from which up to n bytes are read.
// dot is the inital dot value. out will be filled with the result of
// templating and needs to be freed by the caller. Returns 0 on success.
int template_eval_mem(const char* tpl, size_t n, json_value* dot, char** out);
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

Most templating features, besides sub-templates, are implemented.

| Feature                                       | State                                              |
| --------------------------------------------- | -------------------------------------------------- |
| Trim whitespace `{{-` and `-}}`               | :white_check_mark:                                 |
| `{{/* comments */}}`                          | :white_check_mark:                                 |
| Default Textual Representation `{{pipeline}}` | :white_check_mark:                                 |
| Literals                                      | :construction: (Some escape sequences are missing) |
| `{{if pipeline}} T1 {{end}}`                  | :white_check_mark:                                 |
| `{{if pipeline}} T1 {{else}} T0 {{end}}`      | :white_check_mark:                                 |
| `{{if p}} T1 {{else if p}} T0 {{end}}`        | :white_check_mark:                                 |
| `{{range pipeline}} T1 {{end}}`               | :white_check_mark:                                 |
| `{{range pipeline}} T1 {{else}} T0 {{end}}`   | :white_check_mark:                                 |
| `{{break}}`                                   | :white_check_mark:                                 |
| `{{continue}}`                                | :white_check_mark:                                 |
| `{{define}}`                                  | :x:                                                |
| `{{template "name" pipeline}}`                | :x:                                                |
| `{{block "name" pipeline}} T1 {{end}}`        | :x:                                                |
| `{{with pipeline}} T1 {{end}}`                | :white_check_mark:                                 |
| Field access `.a.b.c`                         | :white_check_mark:                                 |
| Variables `$a := 1337`                        | :white_check_mark:                                 |
| `range` with variable initializer             | :white_check_mark:                                 |
| Function invocation `{{ func $value }}`       | :white_check_mark:                                 |
| Pipes `{{ $value \| func }}`                  | :white_check_mark:                                 |

cgotpl parses non-executed templates sloppy.
Syntactical issues in non-executed branches may not lead to an error.

## Functions

| Function | State              |
| -------- | ------------------ |
| and      | :white_check_mark: |
| call     | :x:                |
| html     | :x:                |
| index    | :x:                |
| slice    | :x:                |
| js       | :x:                |
| len      | :white_check_mark: |
| not      | :white_check_mark: |
| or       | :white_check_mark: |
| print    | :x:                |
| printf   | :x:                |
| println  | :x:                |
| urlquery | :x:                |

The c and go standard library may disagree on certain formatting (`printf`) corner-cases.

## Why?

I wanted to make something in C.
