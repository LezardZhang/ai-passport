#include "xigua_cloud_state.h"
#include "xigua_wav.h"
#include "xigua_catalog.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    xigua_cloud_state_t cloud;
    xigua_cloud_init(&cloud,2,2);
    assert(cloud.ids[0]==1 && cloud.ids[1]==2 && cloud.next_seq==3);
    assert(xigua_cloud_floor(&cloud,2,2)==1);
    uint64_t undo[XIGUA_CLOUD_CAPACITY]; memcpy(undo,cloud.ids,sizeof(undo));
    cloud.ids[2]=cloud.next_seq++;
    memcpy(cloud.ids,undo,sizeof(undo));
    assert(cloud.next_seq==4 && xigua_cloud_floor(&cloud,2,2)==1);
    cloud.ids[2]=cloud.next_seq++;
    assert(cloud.ids[2]==4); /* An undone ID cannot be reused. */
    xigua_cloud_init(&cloud,7,32);
    assert(xigua_cloud_floor(&cloud,7,32)==1);
    cloud.ids[7]=cloud.next_seq++;
    assert(xigua_cloud_floor(&cloud,8,32)==2);
    uint8_t header[44]={0};
    memcpy(header,"RIFF",4);memcpy(header+8,"WAVEfmt ",8);memcpy(header+36,"data",4);
    header[4]=40;header[16]=16;header[20]=1;header[22]=1;
    header[24]=0xe0;header[25]=0x2e;header[28]=0xc0;header[29]=0x5d;header[32]=2;header[34]=16;header[40]=4;
    uint32_t bytes=0;assert(xigua_wav_header(header,&bytes)&&bytes==4);
    header[40]=3;assert(!xigua_wav_header(header,&bytes));
    header[40]=4;header[24]=0;assert(!xigua_wav_header(header,&bytes));
    assert(xigua_wav_range("bytes 4140-10043/10044",4096,10000));
    assert(!xigua_wav_range("bytes 44-10043/10044",4096,10000));
    assert(!xigua_wav_range("bytes 4140-10043/10045",4096,10000));
    assert(!xigua_wav_range("bytes 4141-10043/10044",4097,10000));
    assert(!xigua_wav_range("bytes 4140-10043/10044 extra",4096,10000));
    xigua_catalog_track_t tracks[2];
    assert(xigua_catalog_track(&tracks[0],"http://cloud/app","First","http://cloud/app/media/a.wav","audio/wav",false));
    assert(xigua_catalog_track(&tracks[1],"http://cloud/app","Second","http://cloud/app/media/b.wav","audio/wav",false));
    assert(strcmp(tracks[0].url,tracks[1].url)); /* Selection retains each distinct track. */
    assert(!xigua_catalog_track(&tracks[0],"http://cloud/app","Bad","http://cloud/app.evil/media/a.wav","audio/wav",false));
    assert(!xigua_catalog_track(&tracks[0],"http://cloud/app","Bad","http://cloud/app/media/a.mp3","audio/mpeg",false));
    assert(!xigua_catalog_track(&tracks[0],"http://cloud/app","Bad","x","audio/wav",false));
    char long_title[130];
    memset(long_title,'a',94); memcpy(long_title+94,"儿歌",7);
    assert(xigua_catalog_track(&tracks[0],"http://cloud/app",long_title,"http://cloud/app/media/a.wav","audio/wav",true));
    assert(strlen(tracks[0].title)==94 && tracks[0].white); /* No split UTF-8 character. */
    assert(!strcmp(tracks[0].category,"white_noise"));
    assert(xigua_catalog_track_category(&tracks[1],"http://cloud/app","故事","http://cloud/app/media/story.wav","audio/wav","story"));
    assert(!tracks[1].white && !strcmp(tracks[1].category,"story"));
    assert(xigua_catalog_track_category(&tracks[1],"http://cloud/app","古典","http://cloud/app/media/classical.wav","audio/wav","classical"));
    assert(!strcmp(tracks[1].category,"classical"));
    assert(!xigua_catalog_track_category(&tracks[1],"http://cloud/app","未知","http://cloud/app/media/other.wav","audio/wav","podcast"));
    puts("Cloud sequence rollover, undo and WAV boundaries: PASS");
    return 0;
}
