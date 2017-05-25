#!/bin/sh
for i in `seq 1 10000`
do
    echo "==================== test $i ===================="
    ./evdtest_tests -r 1 "$@"
    if [ $? -ne 0 ]; then
        echo "!!!!!!!!!!!!!!!!!!!! test error !!!!!!!!!!!!!!!!!!!!"
        exit 1
    fi
done
