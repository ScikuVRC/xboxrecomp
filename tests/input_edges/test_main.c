#include "input_edges.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char down[10] = {0,0,0,0,255}, up[10]; /* Xbox X */
static void ack(InputEdges *q, uint64_t token, unsigned packet, const unsigned char *b, uint64_t at)
{ input_edges_delivered(q,token); CHECK(input_edges_consume(q,packet,b,at)==token); }

int main(int argc,char **argv)
{
    InputEdges q = {0};
    unsigned char out[10], chord[10];
    uint64_t token;
    unsigned i;
    const char *name = argc > 1 ? argv[1] : "rapid";
    if (!strcmp(name,"rapid")) {
        input_edges_feed(&q,down,1000000); input_edges_feed(&q,up,2000000);
        input_edges_feed(&q,down,3000000); input_edges_feed(&q,up,4000000);
        for(i=0;i<4;i++) {
            token=input_edges_peek(&q,out,5000000+i);
            CHECK(!!input_edges_mask(out)==!(i&1));
            CHECK(input_edges_peek(&q,out,6000000+i)==token); /* peek never consumes */
            ack(&q,token,i+1,out,7000000+i);
        }
        CHECK(q.count==0 && q.expired==0 && q.overflow==0);
    } else if (!strcmp(name,"chord")) {
        memcpy(chord,down,10); chord[2]=255;
        input_edges_feed(&q,down,1); input_edges_feed(&q,chord,2);
        token=input_edges_peek(&q,out,3);
        CHECK(q.count==2 && input_edges_mask(out)==input_edges_mask(chord));
        ack(&q,token,1,out,4); CHECK(q.count==0);
    } else if (!strcmp(name,"commit")) {
        input_edges_feed(&q,down,1); token=input_edges_peek(&q,out,2);
        CHECK(!input_edges_consume(&q,1,out,3) && q.count==1); /* failed TD */
        input_edges_delivered(&q,token+1);
        CHECK(!input_edges_consume(&q,2,out,4) && q.count==1); /* wrong token */
        input_edges_delivered(&q,token);
        CHECK(!input_edges_consume(&q,2,out,5) && q.count==1); /* stale guest packet */
        CHECK(input_edges_consume(&q,3,out,6)==token && q.count==0);
    } else if (!strcmp(name,"age")) {
        input_edges_feed(&q,down,1); input_edges_feed(&q,up,2);
        CHECK(!input_edges_peek(&q,out,INPUT_EDGES_MAX_AGE_NS+2));
        CHECK(!input_edges_mask(out) && q.expired==2 && !q.count);
    } else if (!strcmp(name,"overflow")) {
        for(i=0;i<65;i++) input_edges_feed(&q,(i&1)?up:down,i+1);
        CHECK(q.overflow==65 && !q.count);
        CHECK(!input_edges_peek(&q,out,66) && input_edges_mask(out)==input_edges_mask(down));
    } else if (!strcmp(name,"reset")) {
        input_edges_feed(&q,down,1); token=input_edges_peek(&q,out,2);
        input_edges_reset(&q,up); input_edges_feed(&q,down,3);
        input_edges_delivered(&q,token);
        CHECK(!input_edges_consume(&q,1,down,4));
        CHECK(input_edges_peek(&q,out,5)>token && q.cancelled==1);
    } else if (!strcmp(name,"analog")) {
        memset(chord,0,sizeof chord); chord[9]=100; chord[1]=128;
        input_edges_feed(&q,chord,1); chord[9]=200; input_edges_feed(&q,chord,2);
        CHECK(q.count==1);
        token=input_edges_peek(&q,out,3);
        CHECK(out[9]==200 && (input_edges_mask(out)&(1u<<15)) && (input_edges_mask(out)&(1u<<23)));
        ack(&q,token,1,out,4);
    } else if (!strcmp(name,"threshold")) {
        q.analog_threshold=1;
        memset(chord,0,sizeof chord); chord[8]=1;
        input_edges_feed(&q,chord,1); CHECK(!q.count);
        chord[8]=2; input_edges_feed(&q,chord,2);
        chord[8]=1; input_edges_feed(&q,chord,3);
        CHECK(q.count==2);
        token=input_edges_peek(&q,out,4); CHECK(out[8]==2);
        ack(&q,token,1,out,5);
        token=input_edges_peek(&q,out,6); CHECK(out[8]==1);
        ack(&q,token,2,out,7); CHECK(!q.count);
    } else if (!strcmp(name,"wrap")) {
        input_edges_feed(&q,down,1); token=input_edges_peek(&q,out,2);
        ack(&q,token,0xffffffffu,out,3);
        input_edges_feed(&q,up,4); token=input_edges_peek(&q,out,5); ack(&q,token,0,out,6);
        CHECK(!q.count);
    } else if (!strcmp(name,"ring")) {
        for(i=0;i<100000;i++) {
            input_edges_feed(&q,(i&1)?up:down,i*1000ull);
            token=input_edges_peek(&q,out,i*1000ull+1);
            ack(&q,token,i+1,out,i*1000ull+2);
        }
        CHECK(!q.count && !q.overflow && !q.expired);
    } else CHECK(0);
    printf("PASS %s\n",name); return 0;
}
