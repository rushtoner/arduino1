#!/bin/
import re
#from sets import Set
import sys

#sn01pat = '\$SN01,[\/([A-Z]+)\s+([^\s]+)\s+(HTTP[\/\d\.]*)'
# $SN01,0.9,9,2021-03-23,22:27:04,1,32.47635,-84.95120,129,2.87,5
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}'
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),'
# works with my logged Arduino 1310 data: sn01pat = '[\[\]0-9x]*\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),([-+]?\d+\.\d+),(\d+)'

#01:30:50,2021-03-24,21:29:36,32.47642,-84.95094,118.50,1.36,0.50,360,8,0.00,0.00,0.04,224.00,25.98,100285.56,51.86,86.90,,,100.00,2.72,,,,,,,,,,,,,-116,,
#01:30:55,2021-03-24,21:29:41,32.47641,-84.95093,117.30,1.36,1.81,360,8,0.00,0.00,0.04,236.00,25.97,100282.46,51.81,87.16,,,101.00,2.75,,,,,,,,,,,,,-116,,
#01:31:00,2021-03-24,21:29:46,32.47641,-84.95090,116.60,1.30,1.63,360,8,0.00,0.00,0.05,216.00,25.98,100284.20,52.04,87.01,,,99.00,2.73,,,,,,,,,,,,,-116,,
#01:31:05,2021-03-24,21:29:51,32.47641,-84.95091,116.20,1.30,0.67,360,8,0.00,0.00,0.04,232.00,25.97,100285.16,51.76,86.93,,,101.00,2.74,,,,,,,,,,,,,-116,,

#           01  : 30  : 50  ,2021 - 03  - 24  ,21   :29   :36   ,32.47642  ,-84.95094
sn01pat = '\d{2}:\d{2}:\d{2},\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},(\d+(\.\d+)?),([-+]?\d+(\.\d+)?),.*'
lineCount = 0


def doLine(n, line):
  global lineCount
  #print('line[%d] =\"%s\"' % (n,line))
  result = re.match(sn01pat, line)
  if (result):
    if (False):
      print('Match found: ', result.group())
      j = 0
      print('  group(%d) = %s' % (j, result.group(j)))
      j += 1
      print('  group(%d) = %s' % (j, result.group(j)))
      j += 1
      print('  group(%d) = %s' % (j, result.group(j)))
      j += 1
      print('  group(%d) = %s' % (j, result.group(j)))
    if (lineCount > 0):
      print(',')
    print('    {')
    print('      "type":"Feature",')
    print('      "geometry": {')
    print('        "type":"Point",')
    print('        "coordinates":[%s,%s]' % (result.group(3),result.group(1)))
    print('      },')
    print('      "properties":{"description":"%s"}' % (result.group(0)))
    print('    }')
    lineCount += 1
  else:
    if (False):
      print('  no group: %s' % line)



def doFile(infile):
  lineCount = 0
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
  # doFile('sn01.log')
  #doFile('../LORA-028.LOG')
  #doFile('../DATA314.CSV')
  lineCount = 0
  doFile(sys.argv[1])

#if len(sys.argv) < 2:
#print('Please tell me what file to process.  Please?');
#else:
##processLogFile('../activity_wpas_p01/apps_httpd_access_202001.log')
#processLogFile(sys.argv[1])

