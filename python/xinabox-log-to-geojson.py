#!/bin/
import re
#from sets import Set
import sys

#sn01pat = '\$SN01,[\/([A-Z]+)\s+([^\s]+)\s+(HTTP[\/\d\.]*)'
# $SN01,0.9,9,2021-03-23,22:27:04,1,32.47635,-84.95120,129,2.87,5
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}'
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),'
sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),([-+]?\d+\.\d+),(\d+)'




def doLine(n, line):
  #print('line[%d] =\"%s\"' % (n,line))
  result = re.match(sn01pat, line)
  if (result):
    if (False):
      j = 0
      print('  group(%d) = %s' % (j, result.group(j)))
      j = 3
      print('  group(%d) = %s' % (j, result.group(j)))
      j += 1
      print('  group(%d) = %s' % (j, result.group(j)))
      j += 1
      print('  group(%d) = %s' % (j, result.group(j)))
    if (n > 0):
      print(',')
    print('    {')
    print('      "type":"Feature",')
    print('      "geometry": {')
    print('        "type":"Point",')
    print('        "coordinates":[%s,%s]' % (result.group(4),result.group(3)))
    print('      },')
    print('      "properties":{"description":"%s"}' % (result.group(0)))
    print('    }')
  else:
    if (False):
      print('  no group: %s' % line)


def doFile(infile):
  print('{')
  print('  "type":"FeatureCollection",')
  print('  "features":[')

  #print('infile = ', infile)
  lineCount = 0
  noMatchCount = 0
  exCount = 0
  n = 0
  for lineX in open(infile):
    doLine(n, lineX.rstrip())
    n += 1
  print('  ]')
  print('}')



if __name__ == '__main__':
  #doFile('../LORA.LOG')
  doFile('sn01.log')

#if len(sys.argv) < 2:
#print('Please tell me what file to process.  Please?');
#else:
##processLogFile('../activity_wpas_p01/apps_httpd_access_202001.log')
#processLogFile(sys.argv[1])

