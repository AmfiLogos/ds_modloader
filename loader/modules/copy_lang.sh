#!/bin/zsh
# copy_lang.sh <repo> <commit> <mod dir>: a mod's translations (the repo's top-level lang/*.json at that commit) into
# <mod dir>/lang/. Nothing when that commit has no lang/ folder. lang/ is a subfolder, so it stays out of mod.json
# "files" and out of the mod-set hash: a translation fix never stops players joining each other.
#   copy_lang.sh <folder> - <mod dir>    the same from a checked-out tree (<folder>/lang/*.json)
set -e
R=$1 C=$2 D=$3 n=0
if [[ $C == - ]]; then
  J=($R/lang/*.json(N))
  if (( ${#J} )); then mkdir -p $D/lang; cp $J $D/lang/; n=${#J}; fi
else
  for f in ${(f)"$(git -C $R ls-tree --name-only $C lang/ 2>/dev/null)"}; do
    [[ $f == lang/*.json ]] || continue
    mkdir -p $D/lang; git -C $R show "$C:$f" > $D/$f.tmp; mv -f $D/$f.tmp $D/$f; n=$((n + 1))
  done
fi
(( n )) && echo "lang: $n file(s) -> $D/lang"
exit 0
