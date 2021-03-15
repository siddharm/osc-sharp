/* sine_tbl.c: wavetable sine oscillator */

#include <stdio.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <signal.h>
#include <jack/jack.h>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef jack_default_audio_sample_t sample_t;

static jack_client_t *client = NULL;
//static jack_port_t *port_in = NULL,
static jack_port_t *port_out = NULL;

static jack_nframes_t sr;


/* ------------------perlin noise implementation -----------------*/

//source: https://gist.github.com/nowl/828013
//thank you to nowl on github.

#include <stdio.h>

static int SEED = 0;

static int hash[] = {208,34,231,213,32,248,233,56,161,78,24,140,71,48,140,254,245,255,247,247,40,
                     185,248,251,245,28,124,204,204,76,36,1,107,28,234,163,202,224,245,128,167,204,
                     9,92,217,54,239,174,173,102,193,189,190,121,100,108,167,44,43,77,180,204,8,81,
                     70,223,11,38,24,254,210,210,177,32,81,195,243,125,8,169,112,32,97,53,195,13,
                     203,9,47,104,125,117,114,124,165,203,181,235,193,206,70,180,174,0,167,181,41,
                     164,30,116,127,198,245,146,87,224,149,206,57,4,192,210,65,210,129,240,178,105,
                     228,108,245,148,140,40,35,195,38,58,65,207,215,253,65,85,208,76,62,3,237,55,89,
                     232,50,217,64,244,157,199,121,252,90,17,212,203,149,152,140,187,234,177,73,174,
                     193,100,192,143,97,53,145,135,19,103,13,90,135,151,199,91,239,247,33,39,145,
                     101,120,99,3,186,86,99,41,237,203,111,79,220,135,158,42,30,154,120,67,87,167,
                     135,176,183,191,253,115,184,21,233,58,129,233,142,39,128,211,118,137,139,255,
                     114,20,218,113,154,27,127,246,250,1,8,198,250,209,92,222,173,21,88,102,219};

int noise2(int x, int y)
{
    int tmp = hash[(y + SEED) % 256];
    return hash[(tmp + x) % 256];
}

float lin_inter(float x, float y, float s)
{
    return x + s * (y-x);
}

float smooth_inter(float x, float y, float s)
{
    return lin_inter(x, y, s * s * (3-2*s));
}

float noise2d(float x, float y)
{
    int x_int = x;
    int y_int = y;
    float x_frac = x - x_int;
    float y_frac = y - y_int;
    int s = noise2(x_int, y_int);
    int t = noise2(x_int+1, y_int);
    int u = noise2(x_int, y_int+1);
    int v = noise2(x_int+1, y_int+1);
    float low = smooth_inter(s, t, x_frac);
    float high = smooth_inter(u, v, x_frac);
    return smooth_inter(low, high, y_frac);
}

float perlin2d(float x, float y, float freq, int depth)
{
    float xa = x*freq;
    float ya = y*freq;
    float amp = 1.0;
    float fin = 0;
    float div = 0.0;

    int i;
    for(i=0; i<depth; i++)
    {
        div += 256 * amp;
        fin += noise2d(xa, ya) * amp;
        amp /= 2;
        xa *= 2;
        ya *= 2;
    }

    return fin/div;
}


/* ---------------- tbl ---------------- */

#define TBL_SIZE 512
static sample_t tbl[TBL_SIZE][TBL_SIZE];

static float freq_x = 0;
static float freq_y = 0;


float wrapper(float x) {
  float ret = x;
  while(ret >= 1) ret-=2;
  while (ret < -1) ret+=2;
  return ret;
}


/* map val from range [x0,x1] to [y0,y1] */
#define map(val, x0, x1, y0, y1) \
  ((y0) + ((y1)-(y0)) * (float)((float)(val)-(float)(x0)) / (float)((float)(x1)-(float)(x0)))

/* init wavetable. this should be called before
 * activating client */

static void
tbl_init(void) {
  /* divide [0,2π) into TBL_SIZE evenly spaced samples */
  //float step = 2*M_PI / TBL_SIZE;
  size_t i;
  float x,y;

  for (i = 0; i < TBL_SIZE; ++i) {

    x = map(i, 0.0f, TBL_SIZE, -1.0f, 1.0f);

    for (int j = 0; j < TBL_SIZE; j++) {

      y = map(j, 0.0f, TBL_SIZE, -1.0f, 1.0f);

      //float p = sin(x)*sin(y)*(x-y)*(x-1.0f)*(x+1.0f)*(y-1.0f)*(y+1.0f);
      //float p = sin(1.0f-x*x)*sin(1.0f-y*y)*(1.0f-y*y)*x;
      float p = perlin2d(x, y, 0.1, 7)*(x-1)*(x+1)*(y-1)*(y+1);

      tbl[i][j] = p;
      //printf("%lu,%d is %f\nx and y are: %f, %f\n", i, j, p, x, y);

    }
  }

}


float
interpolate(int v00, int v10, int v01, int v11, int i_fr, int j_fr)
{
  float out;


  float u0 = (1-i_fr)*v00 + i_fr*v10;
  float u1 = (1-i_fr)*v01 + i_fr*v11;
  out = (1-j_fr)*u0 + j_fr*u1;


  return out;
}




static sample_t
tbl_eval(float x, float y) {
  sample_t v00, v10, v01, v11;
  size_t i, j;
  float i_fr, j_fr;
  int N = TBL_SIZE;



  /* 1. wrap x,y to be in the range [-1,1) */
  
  if (x > 1.0f) {
    while (x > 1.0f) x--;
    x = -1.0f + x; 
  }


  if (x < -1.0f) {
    while (x < -1.0f) x++;
    x = 1.0f - x; 
  }

  
  /* 2. map x,y to float index i, j in the range [0, N)
   *    (you might need a new variable here) */

  float newx = map(x, -1.0f, 1.0f, 0.0f, N);
  float newy = map(y, -1.0f, 1.0f, 0.0f, N);


  /* 3. compute integer (i) and fractional part (fr) of float
   *    index i, j */
  //i_fr = newx * TBL_SIZE;
  i_fr = newx;
  i = (size_t) i_fr; /* take floor */
  i_fr = i_fr - i;

  //j_fr = newy * TBL_SIZE;
  j_fr = newy;
  j = (size_t) j_fr; /* take floor */
  j_fr = j_fr - j;


  
  /* 4. find neighboring values */
  v00 = tbl[i][j];
  v10 = tbl[(i+1) % N][j];
  v01 = tbl[i][(j+1) % N];
  v11 = tbl[(i+1) % N][(j+1) % N];


  /* 5. bilinear interpolation with i_fr and j_fr as weights */
  
  float out;


  float u0 = (1-i_fr)*v00 + i_fr*v10;
  float u1 = (1-i_fr)*v01 + i_fr*v11;
  out = (1-j_fr)*u0 + j_fr*u1;


  //int g = tbl[5000][5000];


  return out;

  //return interpolate(v00, v10, v01, v11, i_fr, j_fr);


}



/* ---------------- /tbl ---------------- */


/* ---------------- onproceess callback ---------------- */


//frequency smoothing for freq_x and freq_y
static float
freq_tick_x(void) {
  static float mem = 0;
  mem = 0.001 * freq_x + 0.999 * mem;
  return mem;
}


static float
freq_tick_y(void) {
  static float mem = 0;
  mem = 0.001 * freq_y + 0.999 * mem;
  return mem;
}



static int
on_process(jack_nframes_t nframes, void *arg)
{
  //sample_t *in;
  sample_t *out;
  jack_nframes_t i;
  sample_t x,y;


  float phi_x=0, phi_y=0;

  //in  = jack_port_get_buffer(port_in,  nframes);
  out = jack_port_get_buffer(port_out, nframes);


  //get phi_x and phi_y from the mouse
  //mouse updates freq_x and freq_y everytime it changes

  for (i = 0; i < nframes; ++i) {


    //phi_x += freq_x / sr;
    phi_x += freq_tick_x() / sr;
    phi_x = wrapper(phi_x);

    //phi_y += freq_y / sr;
    phi_y += freq_tick_y() / sr;
    phi_y = wrapper(phi_y);

    //x(phi) = 0.7cos(2*pi*phi + pi/3)
    //y(phi) = 0.35sin(8*pi*phi)
    x = 0.35f*cos(2.0f*M_PI*phi_x + M_PI/6.0f);
    y = 0.7f*sin(8.0*M_PI*phi_y);
    //x = phi_x*cos(2.0f*M_PI*0.9 + M_PI/0.3f);
    //y = phi_y*sin(8.0*M_PI*0.4);
    


    out[i] = tbl_eval(x,y);
    //out[i] = tbl_eval(in[i]);
  }

  return 0;
}


/* ---------------- /onproceess callback ---------------- */





/* ---------------- setup ---------------- */


static void
jack_init(void)
{
  client = jack_client_open("sine", JackNoStartServer, NULL);

  sr = jack_get_sample_rate(client);
  //printf("sample rate of your jack server is: %d\n", sr);

  jack_set_process_callback(client, on_process, NULL);

  //port_in  = jack_port_register(client, "phs", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  port_out = jack_port_register(client, "out", JACK_DEFAULT_AUDIO_TYPE,
                                JackPortIsOutput, 0);

}


static void
jack_finish(void)
{
  jack_deactivate(client);
  jack_client_close(client);
}


/* ---------------- /setup ---------------- */



/*------------------mouse setup ---------------------------*/


static volatile int done = 0;

static void
die(const char *msg)
{
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s\n", msg);
  exit(1);
}

static void
on_signal(int signum)
{
  (void)signum;
  done = 1;
}



/*------------------/mouse setup ---------------------------*/




int
main(void)
{



  jack_init();

  tbl_init();    /* <- must be called before activating client */

  jack_activate(client);


  printf("Jack connection successful! Now, hook this oscillator up on the patchbay.\n");
  



  /*--------mouse stuff-------------*/



  if( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }



  int x = 0, y = 0, prev_x = 0, prev_y = 0;

  int MAX_Y;
  int MAX_X;

  SDL_Rect r;
  if (SDL_GetDisplayBounds(0, &r) != 0) {
      SDL_Log("SDL_GetDisplayBounds failed: %s", SDL_GetError());
      return 1;
  }

  MAX_X = r.w;
  MAX_Y = r.h;

  //printf("width/height of your display: %d/%d\n", MAX_Y, MAX_X);

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    die("fail to init sdl");

  /* catch signal for grace shutdown */
  signal(SIGINT, on_signal);

  printf("Use the mouse to modulate the phase of the orbit. The position of the mouse relative to width and height of the display control the x phase and y phase, respectively.\n");

  printf("\n");

  while (!done) {
    SDL_Event evt;

    while (SDL_PollEvent(&evt));

    SDL_GetGlobalMouseState(&x, &y);
    if (x != prev_x || y != prev_y) {
      //printf("mouse: %d %d\n", x % MAX_X, y % MAX_Y);
      prev_x = x % MAX_X;
      prev_y = y % MAX_Y;

      freq_y = map(prev_y, 0, MAX_Y, 0, 220);
      freq_x = map(prev_x, 0, MAX_X, 0, 440);
    }
  }

  SDL_Quit();



  /*-------- /mouse stuff-------------*/


  /* idle main thread */
  getchar();

  jack_finish();
  return 0;
}
