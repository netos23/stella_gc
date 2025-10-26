#!/bin/sh

err=$(ls | wc -l)
suc=0
echo ""
for file in *; do
  chmod +x $file
  timeout 15s ./$file < 5
  if [ $? -eq 0 ]
  then
    ((suc++))
    ((err--))
    echo "✅test/$file.c: $file"
  else
    echo "::group::❌test/$file.c: $file (failed)"
    echo "Timeout or error reached"
    echo "::endgroup::"
  fi
done
echo ""
if [ $err -eq 0 ]
  then
    echo "🎉 $suc tests passed."
    exit 0
  else
    echo "::error::$suc test passed, $err failed."
    exit 1
fi



