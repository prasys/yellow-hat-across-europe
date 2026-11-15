#!/bin/sh
# Print the original instructions of the lifted code between two addresses (from the comments in build/gen).
#   scripts/dis.sh 4081ba 408260
ROOT=$(cd "$(dirname "$0")/.." && pwd)
awk -v lo="$1" -v hi="$2" '
function hex(s,  i,c,v){v=0;s=toupper(s);for(i=1;i<=length(s);i++){c=index("0123456789ABCDEF",substr(s,i,1))-1;v=v*16+c}return v}
BEGIN{l=hex(lo);h=hex(hi)}
{ if (match($0, /\/\* 0x[0-9A-F]+: [^*]*\*\//)) { c=substr($0,RSTART+3,RLENGTH-6); a=hex(substr(c,3,8)); if(a>=l&&a<=h&&!seen[c]++) print c } }' "$ROOT/build/gen/recomp_0000.c" | sort
