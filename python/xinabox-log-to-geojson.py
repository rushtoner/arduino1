#!/bin/
import re
#from sets import Set
import sys

#sn01pat = '\$SN01,[\/([A-Z]+)\s+([^\s]+)\s+(HTTP[\/\d\.]*)'
# $SN01,0.9,9,2021-03-23,22:27:04,1,32.47635,-84.95120,129,2.87,5
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}'
#sn01pat = '\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),'
# works with my logged Arduino 1310 data: sn01pat = '[\[\]0-9x]*\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),([-+]?\d+\.\d+),(\d+)'

#             date       time UTC F latitude longitude alt
# $SN01,0.9,9,2021-03-23,22:27:04,1,32.47635,-84.95120,129,2.87,5
#                      group0     group1   year  month  day  hour  min   sec  fix? latitude        longitude       alt
sn01pat = '.*\$SN01,\d+(\.\d)?,\d+(\.\d+)?,\d{4}-\d{2}-\d{2},\d{2}:\d{2}:\d{2},\d+,([-+]?\d+\.\d+),([-+]?\d+\.\d+),(\d+)'


lineCount = 0
lastAltM = 0
sequence = 0
maxAltM = 0
maxAltRaw = ''

def doLine(n, line):
  global lineCount
  #print('line[%d] =\"%s\"' % (n,line))
  result = re.match(sn01pat, line)
  if (result):
    geojsonize(result, '')
    lineCount += 1
  else:
    if (False):
      print('  no group: %s' % line)


def geojsonize(result, markerSymbol):
  global lastAltM
  global sequence
  global maxAltM
  global maxAltRaw
  if (False):
    j = 0
    print('  group(%d) = %s' % (j, result.group(j)))
    j = 3
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
  print('        "coordinates":[%s,%s]' % (result.group(4),result.group(3)))
  print('      },')
  print('      "properties":')
  print('        {"rawdata":"%s"' % (result.group(0)))
  altm = int(result.group(5))
  altf = int(float(altm) / 0.3048)
  print('        ,"altitudeMeters":%d' % altm)
  print('        ,"altitudeFeet":%d' % altf)
  if (markerSymbol):
    print('      ,"marker-symbol":"%s"' % markerSymbol)
  if lastAltM != 0:
    if altm > lastAltM:
      # rising
      print('  ,"marker-color":"#00ff00"')
    elif altm < lastAltM:
      print('  ,"marker-color":"#ff0000"')

  print('      ,"sequence":%d' % sequence)
  sequence += 1
  print('        }')
  print('      }')
  if altm > maxAltM:
    maxAltM = altm
  lastAltM = altm


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
  addNotable()
  print('  ]')
  print('}')



if __name__ == '__main__':
  #doFile('../LORA.LOG')
  # doFile('sn01.log')
  #doFile('../LORA-028.LOG')
  #doFile('../DATA314.CSV')
  doFile(sys.argv[1])

#if len(sys.argv) < 2:
#print('Please tell me what file to process.  Please?');
#else:
##processLogFile('../activity_wpas_p01/apps_httpd_access_202001.log')
#processLogFile(sys.argv[1])

