#!/bin/bash

VER=$(cat .last_version.number)

git status
git add .
git commit -m "$VER"
git push origin master
