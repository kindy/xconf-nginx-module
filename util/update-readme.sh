#!/bin/bash

perl util/wiki2pod.pl doc/XconfModuleZh.wiki > /tmp/a.pod && pod2text /tmp/a.pod > README

