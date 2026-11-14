/* All future edge placements, ordinary Edax stability rule. GPL-3.0-or-later. */
#ifndef EDAX_EDGE_STABILITY_CORE_H
#define EDAX_EDGE_STABILITY_CORE_H
static unsigned int edax_edge_stable(unsigned int P,unsigned int O,int n,unsigned short *cache){
 unsigned int key=(P<<n)|O,mask=(1u<<n)-1,E=mask&~(P|O),stable=P;
 if(cache&&cache[key]!=65535)return cache[key];
 if(stable&&E)for(unsigned int X=1;X<=mask&&stable;X<<=1)if(E&X)for(int side=0;side<2&&stable;side++){
  unsigned int p=side?O:P,o=side?P:O,f=0;
  for(int direction=-1;direction<=1;direction+=2){unsigned int z=direction<0?X>>1:(X<<1)&mask,line=0;
   while(z&o){line|=z;z=direction<0?z>>1:(z<<1)&mask;}
   if(z&p)f|=line;
  }
  p|=X|f;o&=~f;
  stable&=side?edax_edge_stable(o,p,n,cache):edax_edge_stable(p,o,n,cache);
 }
 if(cache)cache[key]=(unsigned short)stable;return stable;
}
#endif
