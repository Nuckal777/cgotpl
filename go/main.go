package main

import (
	"encoding/json"
	"os"
	"text/template"
)

func main() {
	var data any
	err := json.Unmarshal([]byte(os.Args[2]), &data)
	if err != nil {
		panic(err)
	}
	tpl, err := template.New("tpl").Parse(os.Args[1])
	if err != nil {
		panic(err)
	}
	err = tpl.Execute(os.Stdout, data)
	if err != nil {
		panic(err)
	}
}
