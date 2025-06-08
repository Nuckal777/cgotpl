#!/usr/bin/env bash

if [ $# -ne 1 ]
then
    echo "Usage: $0 BUILD_DIR" >&2
    exit 1
fi

BUILD_DIR=$1
declare -i FAILS=0

testcase () {
    CGOTPL_OUT=$("${BUILD_DIR}/cli/cgotpl" "$1" "$2" 2>/dev/null)
    CGOTPL_EXIT=$?
    GOTEMPLATE_OUT=$("${BUILD_DIR}/go/gotemplate" "$1" "$2" 2>/dev/null)
    GOTEMPLATE_EXIT=$?
    if [ $CGOTPL_EXIT -ne 0 ] && [ $GOTEMPLATE_EXIT -ne 0 ]; then
        return
    fi
    if [ $CGOTPL_EXIT -ne $GOTEMPLATE_EXIT ]; then
        echo "---"
        printf "exit code mismatch for template '%s' with data '%s'\n" "$1" "$2"
        printf "cgotpl     : %s\n" "${CGOTPL_EXIT}"
        printf "gotemplate : %s\n" "${GOTEMPLATE_EXIT}"
        printf "outputs:\n"
        printf "cgotpl     :'%s'\n" "${CGOTPL_OUT}"
        printf "gotemplate :'%s'\n" "${GOTEMPLATE_OUT}"
        FAILS=$(( FAILS + 1 ))
        return
    fi
    if [ "$CGOTPL_OUT" != "$GOTEMPLATE_OUT" ]; then
        echo "---"
        printf "output mismatch for template '%s' with data '%s'\n" "$1" "$2"
        printf "cgotpl     :'%s'\n" "${CGOTPL_OUT}"
        printf "gotemplate :'%s'\n" "${GOTEMPLATE_OUT}"
        FAILS=$(( FAILS + 1 ))
        return
    fi
}

testcase '{{}}' 'null'
testcase '{{ }}' 'null'
testcase '{{ nil }}' 'null'
testcase 'a {{- }} b' 'null'
testcase 'a {{ -}} b' 'null'
testcase 'a {{- -}} b' 'null'
testcase '{{ . }}{{ ' 'null'
testcase '{{"abc"}}' 'null'
testcase '{{\n}}' 'null'
testcase '{{true}}' 'null'
testcase '{{false}}' 'null'
testcase '{{null}}' 'null'
testcase '{{56}}' 'null'
testcase '{{ -56}}' 'null'
testcase '{{+56}}' 'null'
testcase '{{ -0}}' 'null'
testcase '{{ nan }}' 'null'
testcase '{{ inf }}' 'null'
testcase 'a{{-7}}' 'null'
testcase '{{84.25}}' 'null'
testcase '{{ -84.25}}' 'null'
testcase '{{ 0007.3 }}' 'null'
testcase '{{ -0003.4 }}' 'null'
testcase '{{/* xyz */}}' 'null'
testcase '{{9 /* xyz */}}' 'null'
testcase '{{/* xyz */ 9}}' 'null'
testcase '{{- /* xyz */ -}}' 'null'
testcase '{{-/* xyz */ -}}' 'null'
testcase '{{- /* xyz */-}}' 'null'
testcase '{{/* xyz */ }}' 'null'
testcase '{{ /* xyz */}}' 'null'
testcase '{{.}}' '[]'
testcase '{{.}}' '[7,8,9]'
testcase '{{.}}' '{}'
testcase '{{.}}' '{"a":null}'
testcase '{{ .abc }}' '{}'
testcase '{{ .x }}' '{"x": "y"}'
testcase '{{{}}' 'null'
testcase '{{if}}' 'null'
testcase '{{if false}}' 'null'
testcase '{{end}}' 'null'
testcase '{{if true}}sh{{end}}' 'null'
testcase '{{if false}}sh{{end}}' 'null'
testcase '{{if true}}sh{{else}}ti{{end}}' 'null'
testcase '{{if false}}sh{{else}}ti{{end}}' 'null'
testcase '{{if true}}sh{{end}}{{end}}' 'null'
testcase '{{if false}}sh{{end}}{{end}}' 'null'
testcase '{{if true}}x{{else if false}}y{{else}}z{{end}}' 'null'
testcase '{{if false}}x{{else if true}}y{{else}}z{{end}}' 'null'
testcase '{{if false}}x{{else if false}}y{{else}}z{{end}}' 'null'
testcase '{{if+}}{{end}}' 'null'
testcase '{{ range 20 }} {{.}} {{- end}}' 'null'
testcase '{{ range 7.4 }} {{.}} {{- end}}' 'null'
testcase '{{ range -17 }} {{.}} {{- end}}' 'null'
testcase '{{ range -18.3 }} {{.}} {{- end}}' 'null'
testcase '{{ range 0 }} a {{else}} b {{- end}}' 'null'
testcase '{{ range . -}} {{.}} {{- end }}' '[34, 56]'
testcase '{{ range . -}} {{.}} {{- end }}' '{"a": "a", "aa": "b", "qwre": "c", "f": "h", "z": 1}'
testcase '{{range .}} as {{- break -}} df {{end}}' '[0, 0, 0]'
testcase '{{range .}} as {{- continue -}} df {{end}}' '[0, 0, 0]'
testcase '{{with .x -}} hj{{.}}kl {{- else -}} nub {{- end}}' '{"x": "abcd"}'
testcase '{{with .x -}} hj{{.}}kl {{- else -}} nub {{- end}}' '{"y": "yyy"}'
testcase '{{with .x -}} hj{{.}}kl {{- else with .y -}} a{{.}}z {{- else -}} nub {{- end}}' '{"y": "yyy"}'
testcase '{{with `abc`}} {{ 8 }} {{ with . }} banana {{ end }} {{end}}' 'null'
testcase '{{ $a := "xyz" }}{{ with $a }}{{ $a = "p" }}{{ . }}{{ end }}' 'null'
testcase '{{$}}' '["a", 1]'
testcase '{{$huh}}' '[]'
testcase '{{$test:=`something`}}{{$test}}' 'null'
testcase '{{$h := $i := "yay"}}' 'null'
testcase '{{$err="something"}}' 'null'
testcase '{{ $z := "cba"}}{{ $z = true }}{{ $z }}'
testcase '{{ $abc := 3 }}{{ $abc:=4}}{{$abc}}' 'null'
testcase '{{$a := true}}{{with .}}text{{end}}{{$a}}' 'null'
testcase '{{$a := true}}{{with .}}{{$a := false}}{{$a}}{{end}}{{$a}}' 'null'
testcase '{{$a:=nil}}z' 'null'
testcase '{{ if $a := 2345 }}{{$a}}{{end}}' 'null'
testcase '{{ if $a := $b := 2345 }}{{$a}}{{end}}' 'null'
testcase '{{ if $a := 2345 }}{{$a}}{{end}}{{$a}}' 'null'
testcase '{{ with $a := 4567 }}{{$a}}{{end}}' 'null'
testcase '{{ range $a := 6789 }}{{$a}}{{end}}' 'null'
testcase '{{ range . }} {{ if . }} {{ $x := 7 }}c {{ else }} d{{ $x = 13 }} {{ end }} {{ end }}' '[true, false]'
testcase '{{ range . }} {{ if . }} {{ $x }} {{ end }} {{ $x := 7 }} {{ end }}' '[false, true]'
testcase '{{ range $val := . -}} {{$val}} {{- end }}' '[1,2,3,4,5]'
testcase '{{ range $idx,$val := . -}} {{$idx}}{{$val}} {{- end }}' '[1,2,3,4,5]'
testcase '{{ range $val := . -}} {{$val}} {{- end }}' '{"a":"b","c":"d","e":"f"}'
testcase '{{ range $idx, $val := . -}} {{$idx}}{{$val}} {{- end }}' '{"a":"b","c":"d","e":"f"}'
testcase '{{ range $idx, $val := 986 }}{{$idx}}{{$val}}{{end}}' 'null'
testcase '{{ range $a, $a := . }} {{$a}} {{end}}' '["a", "b"]'
testcase '{{ not true -}}' 'null'
testcase '{{ not false }}' 'null'
testcase '{{ not . }}' 'false'
testcase '{{ not false true "hello" }}' 'null'
testcase '{{ not }}' 'null'
testcase '{{ not "" }}' 'null'
testcase '{{ not "abc" }}' 'null'
testcase '{{ not 0 }}' 'null'
testcase '{{ not nil }}' 'null'
testcase '{{ not 7 }}' 'null'
testcase '{{ not 0.0 }}' 'null'
testcase '{{ not 0.3 }}' 'null'
testcase '{{ not . }}' '[]'
testcase '{{ not . }}' '[1,2,3]'
testcase '{{ not . }}' '{}'
testcase '{{ not . }}' '{"a": 37}'
testcase '{{ not not }}' 'null'
testcase '{{ not "}}" }}' 'null'
testcase '{{ not `}}`}}' 'null'
testcase '{{not (true|not)}}' 'null'
testcase '{{ if not . }} x {{ else }} y {{ end }}' 'true'
testcase '{{ if not . }} x {{ else }} y {{ end }}' 'false'
testcase '{{ (9) }}' 'null'
testcase '{{ ((8)) }}' 'null'
testcase '{{(((9)))}}' 'null'
testcase '{{ ( true ) }}' 'null'
testcase '{{ true ) }}' 'null'
testcase '{{ ( true }}' 'null'
testcase '{{ () }}' 'null'
testcase '{{ ( }}' 'null'
testcase '{{ ( "cba" ) }}' 'null'
testcase '{{ ( "cba" ) 46 }}' 'null'
testcase '{{ 64 ( "cba" ) }}' 'null'
testcase '{{ not (not true) }}' 'null'
testcase '{{ not (not (.)) }}' 'false'
testcase '{{ not ($a := true) }}{{ $a }}' 'null'
testcase '{{ if not (not .) }} x {{ else }} y {{ end }}' 'true'
testcase '{{ if not (not .) }} x {{ else }} y {{ end }}' 'false'
testcase '{{ `a` | not }}' 'null'
testcase '{{ true | not | not | not | not }}' 'null'
testcase '{{ nil | not }}' 'null'
testcase '{{ ( false | not ) }}' 'null'
testcase '{{ 7 | nofunc }}' 'null'
testcase '{{ 7 | not true }}' 'null'
testcase '{{ $a := true | not }}{{ $a }}' 'null'
testcase '{{ ($a := true) | not }}{{ $a }}' 'null'
testcase '{{ if . | not }} x {{ else }} y {{ end }}' 'true'
testcase '{{ if . | not }} x {{ else }} y {{ end }}' 'false'
testcase '{{ not true| not }}' 'null'
testcase '{{ not true | not}}' 'null'
testcase '{{ and 0 0 }}{{ and 1 0 }}{{ and 0 1 }}{{ and 1 1 }}' 'null'
testcase '{{ or 0 0 }}{{ or 1 0 }}{{ or 0 1 }}{{ or 1 1 }}' 'null'
testcase '{{ and .a .b }}' '{a: 0, b: 1}'
testcase '{{ or .a .b }}' '{a: 0, b: 1}'
testcase '{{if or ($f := 1) ($g := 0) }} {{ $g }} {{ end }}' 'null'
testcase '{{ len `zyxcba`}}' 'null'
testcase '{{ len . }}' '[1,2,3,4,5,6,7,8]'
testcase '{{ len . }}' '{"1": 1,"2": 2,"3": 3,"4": 4}'
testcase '{{ len 987 }}' 'null'
testcase '{{ print . }}' 'true'
testcase '{{ print . }}' 'false'
testcase '{{ print . }}' 'null'
testcase '{{ print . }}' '654'
testcase '{{ print . }}' '"cba"'
testcase '{{ print . }}' '[9,8,7]'
testcase '{{ print . }}' '{"h": "i"}'
testcase '{{ print }}' 'null'
testcase '{{ println . }}' '789'
testcase '{{ println }}' 'null'
testcase '{{ $s := println 924 }} {{ $s }}' 'null'

if [ $FAILS -ne 0 ]; then
    printf "\nencountered %d failures\n" $FAILS
    exit 1
fi
