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

testcase '{{"abc"}}' 'null'
testcase '{{\n}}' 'null'
testcase '{{true}}' 'null'
testcase '{{false}}' 'null'
testcase '{{null}}' 'null'
testcase '{{56}}' 'null'
testcase '{{ -56}}' 'null'
testcase '{{84.25}}' 'null'
testcase '{{ -84.25}}' 'null'
testcase '{{.}}' '[]'
testcase '{{.}}' '[7,8,9]'
testcase '{{.}}' '{}'
testcase '{{.}}' '{"a":null}'
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
testcase '{{ range . -}} {{.}} {{- end }}' '[34, 56]'
testcase '{{ range . -}} {{.}} {{- end }}' '{"a": "a", "aa": "b", "qwre": "c", "f": "h", "z": 1}'
testcase '{{range .}} as {{- break -}} df {{end}}' '[0, 0, 0]'
testcase '{{range .}} as {{- continue -}} df {{end}}' '[0, 0, 0]'
testcase '{{with .x -}} hj{{.}}kl {{- else -}} nub {{- end}}' '{"x": "abcd"}'
testcase '{{with .x -}} hj{{.}}kl {{- else -}} nub {{- end}}' '{"y": "yyy"}'
testcase '{{with .x -}} hj{{.}}kl {{- else with .y -}} a{{.}}z {{- else -}} nub {{- end}}' '{"y": "yyy"}'

if [ $FAILS -ne 0 ]; then
    printf "\nencountered %d failures\n" $FAILS
    exit 1
fi
