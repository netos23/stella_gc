#!/bin/sh

err=0
suc=0

for file in *; do
  echo "run test $file"
  chmod +x $file
  timeout 15s ./$file
  if [ $? -eq 0 ]
  then
    echo "✅ test/$file.c: $file"
    ((suc++))
  else
    echo "::group::❌ test/$file.c: $file (failed)"
    echo "Timeout or error reached"
    echo "::endgroup::"
    ((err++))
  fi
done

if [ $err -eq 0 ]
  then
    echo "🎉 $suc tests passed."
  else
    echo "::error::$suc test passed, $err failed."
fi



