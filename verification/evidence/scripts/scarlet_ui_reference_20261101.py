"""Independent submitted-timeline reconstruction for mixed UI PCM receipts."""
import zlib
import numpy as np


class UiCrc:
    def __init__(self,data,loop_start,loop_end,effects):
        self.music=np.frombuffer(data,dtype='<i2');self.start=loop_start;self.end=loop_end
        self.effects={name:np.frombuffer(raw,dtype='<i2') for name,raw in effects.items()}
        self.count=0;self.crc=0;self.events=[]

    def accept(self,count,events,submitted):
        assert self.count<=count<=submitted and events[:len(self.events)]==self.events
        for i,event in enumerate(events):
            assert set(event)=={'kind','start_sample'} and event['kind'] in self.effects
            assert type(event['start_sample']) is int and 0<=event['start_sample']<=submitted
            if i:assert events[i-1]['start_sample']<=event['start_sample']
            if i>=len(self.events):assert event['start_sample']>=self.count
        positions=np.arange(self.count,count,dtype=np.int64)
        indices=np.where(positions<self.end,positions,self.start+(positions-self.end)%(self.end-self.start))
        audio=self.music[indices].astype(np.int32)
        for i,event in enumerate(events):
            clip=self.effects[event['kind']];begin=event['start_sample'];end=begin+len(clip)
            if i+1<len(events):end=min(end,events[i+1]['start_sample'])
            left=max(self.count,begin);right=min(count,end)
            if left<right:audio[left-self.count:right-self.count]+=clip[left-begin:right-begin]
        self.crc=zlib.crc32(np.clip(audio,-32768,32767).astype('<i2').tobytes(),self.crc)
        self.count=count;self.events=[dict(event) for event in events]
        return self.crc

    def accept_delta(self,count,events,base,total,submitted):
        assert base==len(self.events) and total==base+len(events)
        return self.accept(count,self.events+events,submitted)
