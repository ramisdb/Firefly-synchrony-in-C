//
//  firefly.c
//  firefly
//
//  Created by P B Richards on 11/3/19.
//  Copyright © 2019 P B Richards
//
//  Redistribution and use in source and binary forms, with or without modification,
//  are permitted provided that the following conditions are met:
//  1. Redistributions of source code must retain the above copyright notice and this list of conditions.
//  2. Redistributions in binary form must reproduce the above copyright notice.
//  3: Public exhibitions of this code or its execution must cite its author.
//
//  This program was built in Xcode on MacOS but may be compiled on Linux with the following command:
//
//  gcc firefly.c -lm -lpthread -o firefly
//
//  Editing the constants NFLIES and PULSEWIDTH below and recompiling produces interesting results.
//  When this program is run, and the fireflies sync, it produces a file in the current
//  working directory named "flyout.csv" this file contains the firing order timing and position
//  of each of the flies for futher analysis.
//
//  This code isn't written to production quality, sorry, it was a quick hack just to see what was possible.


/*

Mechanism of Rhythmic Synchronous Flashing of Fireflies
John Buck, Elisabeth Buck

Science  22 Mar 1968:
Vol. 159, Issue 3821, pp. 1319-1327
DOI: 10.1126/science.159.3821.1319

"In Thailand, male Pteroptyx malaccae fireflies, congregated in trees, flash in
rhythmic synchrony with a period of about 560 ± 6 msec (at 28° C). Photometric 
and cinematographic records indicate that the range of flash coincidence is of
the order of ± 20 msec. This interval is considerably shorter than the minimum
eye-lantern response latency and suggests that the Pteroptyx synchrony is regulated
by central nervous feedback from preceding activity cycles, as in the human
"sense of rhythm," rather than by direct contemporaneous response to the flashes
of other individuals. Observations on the development of synchrony among Thai fireflies
indoors, the results of experiments on phase-shifting in the American Photinus pyralis
and comparisons with synchronization between crickets and between human beings are
compatible with the suggestion."
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>


#define MAXROW   24    // How many rows and columns are there on the terminal
#define MAXCOL   79

// NOTE: Edit PULSEWITH to change how long a fly's light blinks
//       Change NFLIES to increase or decrease how many there are >100 probably isn't useful

#define NFLIES   64     // how many fireflies to create
#define INTERVAL 1000   // the full duration of one cycle in milliseconds
#define PULSEWIDTH 175 // duration of the light on within one cycle (also milliseconds)

// some "wow I haven't played in terminal mode for 20 years!" commands
#define cls()              printf("\x1b[2J");
#define cursorTo(row,col)  printf("\x1b[%d;%dH",row,col);
#define clearToEOL()       printf("\x1b[K");
#define reverseGreen()     printf("\x1b[7;40;32m");
#define normalColor()      printf("\x1b[0m");

// globals
int quitNow=0;             // tells the threads to quit fireflying, (also synchronizes their start)
int lightOn[NFLIES]={0};    // whos lights are on ?

int masterClock=0;          // this is the clock that the main routine uses
int lightOnTime[NFLIES]={0}; // This records when each fly turned their light on according to masterClock

char *bulb="✳️";

// This struct is used to produce the sorted firing order csv file
#define FlyAccounting struct flyAccountingStruct
FlyAccounting
{
   int flyNo;  // The number this fly was assigned
   int onTime; // what time this fly turns on their light
   int row;    // row position of fly
   int col;    // column position of fly
};


// I can't think in nanoseconds so this is a wrapper to sleep in milliseconds
static void
sleepMilliseconds(int milliseconds)
{
   struct timespec ts;
   
   ts.tv_sec = milliseconds / 1000;
   ts.tv_nsec = (milliseconds % 1000) * 1000000;

   nanosleep(&ts, NULL);
}


// this is a fast random number generator that's good enough for our purposes here

pthread_mutex_t randLock;  // randRange has a static that needs to be protected

static int
randInRange(int minVal,int maxVal)
{
    static uint64_t x=0xCEEDBA11; // Seed the RNG it can't be 0
    int val;
   
    pthread_mutex_lock(&randLock); // protect x
    x ^= x << 13;                // xorshift32 randomizer (see wikipedia)
    x ^= x >> 17;
    x ^= x << 5;
    pthread_mutex_unlock(&randLock); // x is out of the bathroom

    val=(int)(x%(maxVal-minVal+1)+minVal);

    return(val);
}


int   distances[NFLIES][NFLIES]={0}; // matrix that records the distance between any two fireflies
int   flyRowPosition[NFLIES];        // each fly's position on the terminal
int   flyColPosition[NFLIES];
int   canYouSeeMe[NFLIES][NFLIES]={0};// This matrix holds the blink status of between any two flies

pthread_mutex_t canYouSeeLock;  // all the flies read and write to canYouSeeMe this constantly

#define iabs(x) ((x)<0?-(x):(x)) // integer absolute value

// places flies at random row and colum positions and also initializes the distance matrix
void
initFlyPositions()
{
   int i,j;
   float dy,dx;
   
   for(i=0;i<NFLIES;i++) // place them at random on the terminal
   {
      flyRowPosition[i]=randInRange(1,MAXROW);
      flyColPosition[i]=randInRange(1,MAXCOL);
     
       for(j=0;j<i;j++) // make sure no two flies are in same position
         if(flyRowPosition[j]==flyRowPosition[i]
         && flyColPosition[j]==flyColPosition[i]
         ) --i; // occupied, try again
   }
   
   for(i=0;i<NFLIES;i++) // init the distance matrix; I used Manhattan distance here once for fun
      for(j=0;j<NFLIES;j++)
         if(i==j)
            distances[i][j]=0;
         else
            {
              dx=flyColPosition[j]-flyColPosition[i];
              dy=flyRowPosition[j]-flyRowPosition[i];
              // to use Manhattan distance use the line below
              // distances[i][j]=iabs((flyColPosition[j]-flyColPosition[i])+(flyRowPosition[j]-flyRowPosition[i]));
              distances[i][j]=(int) sqrtf((dx*dx)+(dy*dy));
              //distances[i][j]=0; // this removes distance as a factor entirely
            }
}




// Sees if the light is on given the clock and the time the light is supposed to go on

int
isLightOn(int counter,int onTime,int offTime)
{
  if(offTime>onTime) // if the off time doesn't wrap around 0
  {
    if(counter>=onTime && counter<offTime)
      return(1);
    
    return(0);
  }
  
  if(counter>=onTime || counter<offTime)
   return(1);
  
  return(0);
}

// this is the code for an individual firefly that factors distance
void *
remoteFirefly(void *arg)
{
  int    myFlyNo=*(int *)arg; // used as index into distance and
  int    counter=0;
  float  sumTimes=0.0;
  float  lightSeenCount=0.0;
  int i;
  int onTime=0;
  int offTime=PULSEWIDTH;
  int myLight=0;
   
  while(quitNow) // synchronize the start by waiting for master thread to set this to 0
      sleepMilliseconds(1);
   
  sleepMilliseconds(randInRange(0,500)); // now wait some randomized amount of time before starting
   
  while(!quitNow) // as long as the master thread doesn't set this to 0 keep going
  {
      if(counter==offTime)
      {
         lightOn[myFlyNo]=0;
         myLight=0;
      }
     
     
      if(counter==onTime)
      {
         lightOn[myFlyNo]=1;
         lightOnTime[myFlyNo]=masterClock;
         myLight=1;
      }
     
      pthread_mutex_lock(&canYouSeeLock); // protect canYouSeeMe matrix
     
      for(i=0;i<NFLIES;i++)
      {

        
        // A fly 5 units away won't see my light for 5 ticks of my counter
        // So, if counter-distance is between onTime and offTime then they can see my light
        int theirCounter=counter-distances[myFlyNo][i];
        
        if(isLightOn(theirCounter,onTime,offTime))
            canYouSeeMe[myFlyNo][i]=1;
        else
            canYouSeeMe[myFlyNo][i]=0;
         
        if(i!=myFlyNo && canYouSeeMe[i][myFlyNo]) // if I see someone elses light
        {
         /*
          ++lightSeenCount;
          sumTimes+=counter; // add when I saw the light to the sum
         */
          
          float distance=distances[i][myFlyNo];
          sumTimes+= ((float)counter*distance);
          lightSeenCount+=(distance);
          
        }
      }
      pthread_mutex_unlock(&canYouSeeLock); // unlock canYouSeeMe
     
      if(counter++ ==INTERVAL)
      {
        int meanTime=(int)(sumTimes/lightSeenCount);
        
        onTime=meanTime-(PULSEWIDTH>>1); // center of the pulse
   
        if(onTime<0)  // if above resulted in a clock underflow
         onTime=INTERVAL+onTime;
        
        offTime=(onTime+PULSEWIDTH)%INTERVAL;
        
        sumTimes=0.0;
        lightSeenCount=0;
        
        counter=0;
      }

    sleepMilliseconds(1);
  }
  pthread_exit(NULL);
}

// compares two fly accounting stucts
int
faCmp(const void *va,const void *vb)
{
  FlyAccounting *a=(FlyAccounting *)va;
  FlyAccounting *b=(FlyAccounting *)vb;
  
  if(a->onTime>b->onTime)
    return(1);
  if(a->onTime<b->onTime)
    return(-1);
  if(a->row>b->row)
   return(1);
  if(a->row<b->row)
   return(-1);
  if(a->col>b->col)
   return(1);
  if(a->col>b->col)
   return(-1);

   return(0);
}

// sees if all the flies are on at the same time
int
inSync()
{
   int i,j;
   int count=0;
   for(i=0;i<NFLIES;i++)
      for(j=0;j<NFLIES;j++)
         if(canYouSeeMe[i][j])
            ++count;
   
 if(count==NFLIES*NFLIES)
   return(1);
   
 return(0);
}

// this is the master thread for the distant fireflies
int
distantFireflies()
{
   int i,j;
   int  id[NFLIES];
   pthread_t tid[NFLIES];
   
   initFlyPositions();

   if(pthread_mutex_init(&randLock, NULL) != 0) // create a lock for the random number generator
   {
        printf("randLock mutex init has failed\n");
        return 1; 
   }
   if(pthread_mutex_init(&canYouSeeLock, NULL) != 0) // create a lock for the canYouSeeMe matrix
   {
        printf("canYouSeeLock mutex init has failed\n");
        return 1; 
   }
   
   quitNow=1;
   
   for(i=0;i<NFLIES;i++) // create a bunch of independent firefly() threads
   {
     id[i]=i;
     pthread_create(&tid[i], NULL,&remoteFirefly,(void *)&id[i]);
   }

   quitNow=0;
   cls();
   
   
   for(i=0;i<60000;i++) // watch the flies for a minute (based on 1ms loop time)
   {
      for(j=0;j<NFLIES;j++)
      {
         cursorTo(flyRowPosition[j],flyColPosition[j]);
         /*
         sprintf(s,"%d",j);
         printf("%s", lightOn[j]?bulb:s);
         */
         if(lightOn[j])
         {
           reverseGreen();
           printf("%d",j);
           normalColor();
         }
         else printf("%d",j);
         
         fflush(stdout);
      }
      
      if(NFLIES<21)
      {
      int k;
      cursorTo(1,MAXCOL+20);
      for(k=0;k<NFLIES;k++)
         printf("%d",k%10);
      for(j=0;j<NFLIES;j++)
      {
         cursorTo(j+2,MAXCOL+18);
         printf("%2d",j);
         for(k=0;k<NFLIES;k++)
         {
           cursorTo(j+2,k+MAXCOL+20);
           if(canYouSeeMe[j][k])
            {
               reverseGreen();
               printf(" ");
               normalColor();
            }
            else printf(".");
         
            fflush(stdout);
         }
         
        }
      }
      cursorTo(MAXROW,MAXCOL+1);
      
      fflush(stdout);
      if(i>30000 && inSync())
         break;
      sleepMilliseconds(1);
      ++masterClock;
      if(masterClock==INTERVAL)
         masterClock=0;
   }
   
   quitNow=1;                 // instruct the firefly () threads to exit
   FILE *wasStdout=stdout;
   stdout=fopen("flyout.csv","w");
   printf("\n");
   for(i=0;i<NFLIES;i++)
         printf("% 4d, % 2d, % 2d, % 2d\n",lightOnTime[i],i,flyRowPosition[i],flyColPosition[i]);
   printf("\n");
   fclose(stdout);
   stdout=wasStdout;
   
   FlyAccounting *fa=calloc(NFLIES,sizeof(FlyAccounting));
   if(fa!=NULL)
   {
      for(i=0;i<NFLIES;i++) // copy the fly info into the stat struct
      {
         fa[i].flyNo=i;
         fa[i].onTime=lightOnTime[i];
         fa[i].row=flyRowPosition[i];
         fa[i].col=flyColPosition[i];
      }
      
      qsort(fa,NFLIES,sizeof(FlyAccounting),faCmp); // sort by firing order of master thread clock
      
      FILE *wasStdout=stdout; // redirect stdout for a second
      stdout=fopen("flyout.csv","w");
     
       printf("Time,N,Row,Col\n"); // make the csv file
      for(i=0;i<NFLIES;i++)
         printf("% 4d, % 2d, % 2d, % 2d\n",fa[i].onTime,fa[i].flyNo,fa[i].row,fa[i].col);
      fclose(stdout);
      stdout=wasStdout;
      
      cursorTo(1,1);
      printf("Firing Sequence replay in slow motion. Press [return] to continue.");
      getchar();
      
      for(j=0;j<2;j++)
      {
         cls();
         for(i=0;i<NFLIES;i++)
         {
           cursorTo(fa[i].row,fa[i].col);
           reverseGreen();
           printf("%d",fa[i].flyNo);
           normalColor();

           fflush(stdout);
           sleepMilliseconds(300);
           cursorTo(fa[i].row,fa[i].col);
           printf("%d",fa[i].flyNo);
         }
      }
     free(fa);
   }
  
   
   sleepMilliseconds(INTERVAL*2);   // wait long enough to ensure they're all dead
   pthread_mutex_destroy(&randLock);
   cursorTo(MAXROW+1,1);
   printf("\n");
   
   return 0;
  
 return 1;
}


// From here to main() is the code for the first type of firefly

void *
firefly(void *arg)
{
  int *myLight=(int *)arg;
  int i;
  int clock=0;
  float lightSeenAt=0;
  int   lightCount=0;
  int   darkCount=0;
  
  int   turnMyLightOnAt=randInRange(0,500);  // how many ms until I turn my light on
  int   myLightDuration=randInRange(PULSEWIDTH-5,PULSEWIDTH+5); // pick a number around the pulse time
  int   howLongHasMyLightBeenOn=0;
  int   myCycleTime=randInRange(INTERVAL-1,INTERVAL+1);  // this isn't much variation
  
  /* Note: myCycleTime is the "expected" group tempo if more math was done to adjust this fly's
    cycle time dynamically to phase lock into all the other's flashes it should be paossible to
    start with much more allowed variation in the initial myCycleTime
  */
  sleepMilliseconds(randInRange(0,500)); // set my alarm clock to wake up at a different time from everyone else
  
  while(!quitNow) // keep going until the master thread turns on the quit flag
  {
    if(clock==turnMyLightOnAt) // if it is time to turn my light on
       *myLight=1;
    
    if(*myLight) // if my light is on
    {
      ++howLongHasMyLightBeenOn;
      if(howLongHasMyLightBeenOn==myLightDuration)
      {
         *myLight=0;
         howLongHasMyLightBeenOn=0;
      }
    }
    
    for(i=0;i<NFLIES;i++)
    {
      if(i!=*myLight) // change to if(1) to count my own light or (i!=*myLight) to remove myself from the light count
      {
         if(lightOn[i])
         {
            lightSeenAt+=clock; // add the sum of the clock flash locations so we can get mean
            ++lightCount;
         }
         else ++darkCount; // this is for the optional code that is commented out below
      }
    }
    
     sleepMilliseconds(1); // this is the attainable sync resolution
     ++clock;
     
     if(clock>=myCycleTime) // start my clock over
     {
       int meanBlink=(int)(lightSeenAt/(float)lightCount);
       
       turnMyLightOnAt=meanBlink-(PULSEWIDTH>>1); // center my light's pulse on the mean
       
       if(turnMyLightOnAt<0) // check for modulus underflow and correct if needed
         turnMyLightOnAt=INTERVAL-turnMyLightOnAt;

/*  I tried this code to close in on a tighter sync. I'm not sure how much it helped.
    
          float lightDarkRatio=(float)lightCount/(float)darkCount;
       float myRatio=(float)myLightDuration/(float)myCycleTime;
       float offByThisMuch=fabs((myRatio-lightDarkRatio)/lightDarkRatio);
       
       if(myRatio<lightDarkRatio && offByThisMuch>0.05)
          myCycleTime++;
       else
       if(myRatio>lightDarkRatio && offByThisMuch>0.05)
          myCycleTime--;
*/
       clock=0;
     }
  }

  pthread_exit(NULL);
}



// this generates a bunch a fireflies and then watches them for awhile

int
makeFireflies()
{
   int i,j;
   int   newRowAt=0;
   
   pthread_t tid[NFLIES];
   
   if(pthread_mutex_init(&randLock, NULL) != 0) // create a lock for the random number generator
   {
        printf("mutex init has failed\n");
        return 1; 
   }
   

   for(i=0;i<NFLIES;i++) // create a bunch of independent firefly() threads
     pthread_create(&tid[i], NULL,&firefly,(void *)&lightOn[i]);

   cls();

   if(NFLIES>5)               // make a square array of indicators if there's many
     newRowAt=sqrtf(NFLIES);
    
   for(i=0;i<20000;i++) // watch the flies for 20 seconds (based on 1ms loop time)
   {
      cursorTo(10,1);
      for(j=0;j<NFLIES;j++)
      {
         printf(" %s %s", lightOn[j]?bulb:" ",j+1==NFLIES?" ":" ");
         if(newRowAt && (j+1)%newRowAt==0)
           printf("\n\n");
         fflush(stdout);
      }
      sleepMilliseconds(1);
   }
   
   quitNow=1;                 // instruct the firefly () threads to exit
   sleepMilliseconds(2000);   // wait long enough to ensure they're all dead
   pthread_mutex_destroy(&randLock);
   printf("\n");
  return(0);
}




void
dumpFly()
{
  int i,j;
  //initFlyPositions();
   
 
 for(i=0;i<NFLIES;i++)
    printf("% 2d ",flyRowPosition[i]);
 printf("\n");
 
 for(i=0;i<NFLIES;i++)
    printf("% 2d ",flyColPosition[i]);
   
 printf("\n"); printf("\n");
 
  for(i=0;i<NFLIES;i++)
  {
   for(j=0;j<NFLIES;j++)
      printf("%2d ",distances[i][j]);
    printf("\n");
  }
  printf("\n");
}


// places text nicely on the terminal and then waits for a character
void
putScreen(char *p)
{
  int col=1;
  cls();
  cursorTo(1,1);
  for(;*p;p++)
  {
    putchar(*p);
    if(*p=='\n') col=1;
    if(col++>70)
    {
      if(*p==' ')
      {
        putchar('\n');
        col=0;
      }
    }
  }
  cursorTo(MAXROW,1);
  printf("Press [return] key to continue.");
  getchar();
}

int main(int argc, const char * argv[])
{
  
 putScreen("Fireflies:\n\nHere are 64 fireflies. Each firefly is slightly different, and is an independently executing\
 program thread. Each firefly is started at a random time and has a cycle time that is slighty different from\
 the others. An individual firefly is unaware of how many other fireflies there are and can only see if others\
 are flashing or not. The master thread starts all the fireflies and then displays when they are blinking\
 on the terminal.\n\nThis program was to demonstrate & test the syncing ability. Each one flashes for 25 0ms +/-5ms and\
 have an overall cycle of 1 second +/- 1ms\n\nThey tend to become synchronous within 5 cycles."
 );
 
  makeFireflies();
  
  putScreen("Fireflies with distance and flash strength:\n\n\
  In this instance we have 64 identical fireflies placed at random positions and are started at random times from 0 to 500ms.\
  In this case however an individual firefly will see the light from every other firefly with a\
  delay proportional to their individual distances apart and a strength inversely proportional to\
  their distances.\n\nThese fireflies take a bit longer to develop their pattern synchrony.\
  \n\nNotice how several occasionally fall out of sync, the reason for this is not known.\
  They are numbered so that their flash order can be examined later."
  );
  
  distantFireflies();
  cls();
  printf("Thanks for watching :) \n");
  return(0);
}
