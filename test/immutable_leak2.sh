#!/bin/bash

echo 1..1

if $RC $srcdir/immutable_leak2.rc 2>&1 | grep -q -F 'assignment leaks mutable pointers'
then
    echo 'ok 1 - immutable_leak2'
else
    echo 'not ok 1 - immutable_leak2'
fi
