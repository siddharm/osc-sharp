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


/* ---------------- tbl ---------------- */

#define TBL_SIZE 512
static sample_t tbl[TBL_SIZE][TBL_SIZE];

static float freq_x = 0;
static float freq_y = 0;


float wrapper(float x) {
  float ret = x;
  while(ret >= 1) ret--;
  while (ret < 0) ret++;
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
      float p = sin(1.0f-x*x)*sin(1.0f-y*y)*(1.0f-y*y)*x;
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


    phi_x += freq_x / sr;
    phi_x = wrapper(phi_x);

    phi_y += freq_y / sr;
    phi_y = wrapper(phi_y);

    //x(phi) = 0.7cos(2*pi*phi + pi/3)
    //y(phi) = 0.35sin(8*pi*phi)
    x = 0.7f*cos(2.0f*M_PI*phi_x + M_PI/0.3f);
    y = 0.7f*sin(8.0*M_PI*phi_y);
    //x = phi_x*cos(2.0f*M_PI*0.9 + M_PI/0.3f);
    //y = phi_y*sin(8.0*M_PI*0.4);
    


    out[i] = tbl_eval(x,y);
    //out[i] = tbl_eval(in[i]);
  }

  return 0;
}






/* ---------------- /setup ---------------- */


static void
jack_init(void)
{
  client = jack_client_open("sine", JackNoStartServer, NULL);

  sr = jack_get_sample_rate(client);
  printf("sr is %d: ", sr);

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



/*------------------mouse setup ---------------------------*/




int
main(void)
{

  printf("\ngot here 1\n");

  jack_init();
  printf("\ngot here 2\n");
  tbl_init();    /* <- must be called before activating client */
  printf("\ngot here 3\n");
  jack_activate(client);
  printf("\ngot here 4\n");


  /*--------mouse stuff-------------*/

  printf("\ngot here 4.a\n");

  if( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }

  printf("\ngot here 5\n");

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

  printf("width/height: %d %d\n", MAX_Y, MAX_X);


  if (SDL_Init(SDL_INIT_VIDEO) != 0)
    die("fail to init sdl");

  /* catch signal for grace shutdown */
  signal(SIGINT, on_signal);

  printf("\ngot here 6\n");

  while (!done) {
    SDL_Event evt;

    while (SDL_PollEvent(&evt));

    SDL_GetGlobalMouseState(&x, &y);
    if (x != prev_x || y != prev_y) {
      //printf("mouse: %d %d\n", x % MAX_X, y % MAX_Y);
      prev_x = x % MAX_X;
      prev_y = y % MAX_Y;

      freq_y = map(prev_y, 0, MAX_Y, 0, 4000);
      freq_x = map(prev_x, 0, MAX_X, 0, 4000);
    }
  }

  SDL_Quit();

printf("\ngot here 7\n");
  /*--------mouse stuff-------------*/


  /* idle main thread */
  getchar();

  jack_finish();
  return 0;
}
