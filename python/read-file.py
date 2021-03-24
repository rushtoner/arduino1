#!/bin/
import re
#from sets import Set
import sys

def doLine(n, line):
  print('line[%d] =\"%s\"' % (n,line))


def readFile(infile):
  print('infile = ', infile)
  lineCount = 0
  noMatchCount = 0
  exCount = 0
  n = 0
  for lineX in open(infile):
    doLine(n, lineX.rstrip())
    n += 1



if __name__ == '__main__':
  readFile('../LORA.LOG')

#if len(sys.argv) < 2:
#print('Please tell me what file to process.  Please?');
#else:
##processLogFile('../activity_wpas_p01/apps_httpd_access_202001.log')
#processLogFile(sys.argv[1])

