#!/bin/bash

VER=$(cat .last_version.number)

git init
git status
git add .
git commit -m "$VER"
git push -u origin master
