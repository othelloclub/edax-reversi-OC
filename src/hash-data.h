/* Shared Edax transposition-data logic. GPL-3.0-or-later.
 * Rule adapters supply score limits and the non-move coordinate only.
 * HashData/HashStoreData are defined by hash.h before this header.
 */
#ifndef EDAX_HASH_DATA_H
#define EDAX_HASH_DATA_H
#include <assert.h>
static inline unsigned int hash_data_level(const HashData *data){
#if USE_TYPE_PUNING
 return data->wl.ui;
#else
 return ((unsigned int)data->wl.c.date<<24)|((unsigned int)data->wl.c.cost<<16)|((unsigned int)data->wl.c.selectivity<<8)|data->wl.c.depth;
#endif
}
static inline void hash_data_update(HashData *data,const HashStoreData *storedata,int score_min)
{
 int score=storedata->score;
 if(score<storedata->beta&&score<data->upper)data->upper=(signed char)score;
 if(score>storedata->alpha&&score>data->lower)data->lower=(signed char)score;
 if((score>storedata->alpha||score==score_min)&&data->move[0]!=storedata->data.move[0]){
  data->move[1]=data->move[0];data->move[0]=storedata->data.move[0];
 }
 if(storedata->data.wl.c.cost>data->wl.c.cost)data->wl.c.cost=storedata->data.wl.c.cost;
}
static inline void hash_data_upgrade(HashData *data,const HashStoreData *storedata,int score_min,int score_max)
{
 int score=storedata->score;
 data->upper=(signed char)(score<storedata->beta?score:score_max);
 data->lower=(signed char)(score>storedata->alpha?score:score_min);
 if((score>storedata->alpha||score==score_min)&&data->move[0]!=storedata->data.move[0]){
  data->move[1]=data->move[0];data->move[0]=storedata->data.move[0];
 }
 data->wl.us.selectivity_depth=storedata->data.wl.us.selectivity_depth;
 if(storedata->data.wl.c.cost>data->wl.c.cost)data->wl.c.cost=storedata->data.wl.c.cost;
 assert(data->upper>=data->lower);
}
static inline void hash_data_new(HashData *data,const HashStoreData *storedata,int score_min,int score_max,int no_move)
{
 int score=storedata->score;
 data->upper=(signed char)(score<storedata->beta?score:score_max);
 data->lower=(signed char)(score>storedata->alpha?score:score_min);
 data->move[0]=(unsigned char)(score>storedata->alpha||score==score_min?storedata->data.move[0]:no_move);
 data->move[1]=(unsigned char)no_move;data->wl=storedata->data.wl;
 assert(data->upper>=data->lower);
}
#endif
