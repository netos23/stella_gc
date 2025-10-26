#!/bin/sh

err=0
suc=0
echo ""
for file in *; do
  chmod +x $file
  timeout 15s ./$file
  if [ $? -eq 0 ]
  then
    ((suc++))
    echo "✅test/$file.c: $file"
  else
    ((err++))
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



