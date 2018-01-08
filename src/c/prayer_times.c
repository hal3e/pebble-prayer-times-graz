#include <pebble.h>

#define TEA_TEXT_GAP 14
#define NUM_PREY_TIMES 6

///--------------------------------------------------------------------------------------------------///
static Window *s_menu_window, *s_countdown_window, *s_wakeup_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_status_text_layer, *s_error_text_layer, *s_salah_text_layer, *s_countdown_text_layer,
                 *s_cancel_text_layer;
static BitmapLayer *s_bitmap_layer;
static GBitmap *s_tea_bitmap;

static WakeupId s_wakeup_id = -1;
static time_t s_wakeup_timestamp = 0;
static char s_salah_text[32];
static char s_time_text[6];
static char s_date_text[11];
static char s_countdown_text[32];

///--------------------------------------------------------------------------------------------------///
typedef struct
{
  uint8_t hour;
  uint8_t minute;
} Salah;

///--------------------------------------------------------------------------------------------------///
typedef struct
{
  Salah salahs_ [NUM_PREY_TIMES];
} Day;

///--------------------------------------------------------------------------------------------------///
int8_t next_prayer_time = -1;

///--------------------------------------------------------------------------------------------------///
typedef struct {
  char name[16];  // Name of this tea
  int mins;       // Minutes to steep this tea
} TeaInfo;

///--------------------------------------------------------------------------------------------------///
// Array of different teas for tea timer
// {<Tea Name>, <Brew time in minutes>}
TeaInfo tea_array[] = {
  {"Green Tea", 1},
  {"Black Tea", 2},
  {"Oolong Tea", 3},
  {"Darjeeling", 4},
  {"Herbal Tea", 5},
  {"Mate Tea", 6},
  {"Chai Tea", 10}
};

///--------------------------------------------------------------------------------------------------///
enum {
  PERSIST_WAKEUP // Persistent storage key for wakeup_id
};

///--------------------------------------------------------------------------------------------------///
Day day_;
struct tm tm_;

///--------------------------------------------------------------------------------------------------///
// Forward declarations
static void read_current_day();
static void select_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index, void *callback_context);
static uint16_t get_sections_count_callback(struct MenuLayer *menulayer, uint16_t section_index, void *callback_context);
static void draw_row_handler(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context);
static void menu_window_load(Window *window);
static void menu_window_unload(Window *window);
static void timer_handler(void *data);
static void countdown_back_handler(ClickRecognizerRef recognizer, void *context);
static void countdown_cancel_handler(ClickRecognizerRef recognizer, void *context);
static void countdown_click_config_provider(void *context);
static void countdown_window_load(Window *window);
static void countdown_window_unload(Window *window);
static void wakeup_click_handler(ClickRecognizerRef recognizer, void *context);
static void wakeup_click_config_provider(void *context);
static void wakeup_window_load(Window *window);
static void wakeup_window_unload(Window *window);
static void wakeup_handler(WakeupId id, int32_t reason);
static void init(void);
static void deinit(void);

///--------------------------------------------------------------------------------------------------///
int main(void) {
  init();
  app_event_loop();
  deinit();
}

///--------------------------------------------------------------------------------------------------///
static void read_current_day(){
          // Load the current day

   time_t t = time(NULL);
   tm_ = *localtime(&t);

   char month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

   ResHandle handle = resource_get_handle(RESOURCE_ID_SALAH);

   int specific_day = -1;
   for(int m = 0; m < tm_.tm_mon; m++)
      specific_day += month[m];

   specific_day += tm_.tm_mday;
  
  // Read the specific day from the raw file
  uint8_t* buffer = (uint8_t*)malloc(sizeof(Day));
  resource_load_byte_range(handle, specific_day * sizeof(Day), buffer, sizeof(Day));
  memcpy(&day_, buffer, sizeof(Day));
  
  // Get index of the next prayer time
  for(size_t i = 0; i<NUM_PREY_TIMES; i++)
  {
    if( (day_.salahs_[i].hour == tm_.tm_hour && day_.salahs_[i].minute > tm_.tm_min) || day_.salahs_[i].hour >  tm_.tm_hour )
    {
      next_prayer_time = i;
      break;
    }
  }

  // If next_prayer_time was not updated it means we are past isha of the current day and we will move to the next day
  if(next_prayer_time == -1)
  {
    next_prayer_time = 0;
    specific_day += 1;
    tm_.tm_mday += 1;
    
    // If we went over the current month, update month and set day to 1
    if(tm_.tm_mday > month[tm_.tm_mon])
    {
      tm_.tm_mday = 1;
      tm_.tm_mon += 1;
      
      // If we went over the current year, update year and set month to 0
      if(tm_.tm_mon > 11)
      {
        tm_.tm_mon = 0;
        tm_.tm_year += 1;
      }
    }
    
    resource_load_byte_range(handle, specific_day * sizeof(Day), buffer, sizeof(Day));
    memcpy(&day_, buffer, sizeof(Day));  
  }
  
  free(buffer);
  
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "day: %d.%d.%d\n\n", tm_.tm_mday, tm_.tm_mon + 1, tm_.tm_year + 1900);

//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  fajr:     %02d:%02d\n", day_.salahs_[0].hour, day_.salahs_[0].minute);
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  sunrise:  %02d:%02d\n", day_.salahs_[1].hour, day_.salahs_[1].minute);
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  zuhr:     %02d:%02d\n", day_.salahs_[2].hour, day_.salahs_[2].minute);
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  asr:      %02d:%02d\n", day_.salahs_[3].hour, day_.salahs_[3].minute);
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  magrib:   %02d:%02d\n", day_.salahs_[4].hour, day_.salahs_[4].minute);
//   APP_LOG(APP_LOG_LEVEL_DEBUG, "  isha:     %02d:%02d\n\n", day_.salahs_[5].hour, day_.salahs_[5].minute);
  
}

///--------------------------------------------------------------------------------------------------///
static void select_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index,
                            void *callback_context) {
  // If we were displaying s_error_text_layer, remove it and return
  if (!layer_get_hidden(text_layer_get_layer(s_error_text_layer))) {
    layer_set_hidden(text_layer_get_layer(s_error_text_layer), true);
    return;
  }

  // Wakeup time is a timestamp in the future
  // so time(NULL) + delay_time_in_seconds = wakeup_time
  time_t wakeup_time = time(NULL) + tea_array[cell_index->row].mins * 60;

  // Use the tea_array index as the wakeup reason, so on wakeup trigger
  // we know which tea is brewed
  s_wakeup_id = wakeup_schedule(wakeup_time, cell_index->row, true);

  // If we couldn't schedule the wakeup event, display error_text overlay
  if (s_wakeup_id <= 0) {
    layer_set_hidden(text_layer_get_layer(s_error_text_layer), false);
    return;
  }

  // Store the handle so we can cancel if necessary, or look it up next launch
  persist_write_int(PERSIST_WAKEUP, s_wakeup_id);

  // Switch to countdown window
  window_stack_push(s_countdown_window, false);
}

///--------------------------------------------------------------------------------------------------///
static uint16_t get_sections_count_callback(struct MenuLayer *menulayer, uint16_t section_index, void *callback_context) {
  return NUM_PREY_TIMES + 1;
}

///--------------------------------------------------------------------------------------------------///
#ifdef PBL_ROUND
static int16_t get_cell_height_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  return 55;
}
#endif

///--------------------------------------------------------------------------------------------------///
static void draw_row_handler(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                             void *callback_context) {
  char* name;
  switch(cell_index->row) {
   case 0 :
    snprintf(s_date_text, sizeof(s_date_text), "%02d.%02d.%d", tm_.tm_mday, tm_.tm_mon + 1, tm_.tm_year + 1900);
    name = s_date_text;
    break;
   case 1 :
      name = "Fajr";
      break;
   case 2 :
      name = "Sunrise";
      break;
    case 3  :
      name = "Zuhr";
      break;
    case 4  :
      name = "Asr";
      break;
    case 5  :
      name = "Magrib";
      break;
    case 6  :
      name = "Isha";
      break;
   default : 
     name = "noName";
  }

  int text_gap_size = TEA_TEXT_GAP - strlen(name);

  // Using simple space padding between name and s_salah_text for appearance of edge-alignment
  if(cell_index->row != 0)
  {
    snprintf(s_salah_text, sizeof(s_salah_text), "%s%*s%02d:%02d", PBL_IF_ROUND_ELSE("", name),
             PBL_IF_ROUND_ELSE(0, text_gap_size), "", day_.salahs_[cell_index->row - 1].hour, day_.salahs_[cell_index->row  - 1].minute);
    
    menu_cell_basic_draw(ctx, cell_layer, PBL_IF_ROUND_ELSE(s_salah_text, name),
                       PBL_IF_ROUND_ELSE(name, NULL), NULL);    
  }
  else {
    menu_cell_basic_draw(ctx, cell_layer,NULL , name, NULL);
  }

  
//   menu_cell_basic_draw(ctx, cell_layer, name,
//                       NULL, NULL);
}


///--------------------------------------------------------------------------------------------------///
static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  
  menu_layer_set_highlight_colors(s_menu_layer, GColorPictonBlue, GColorBlack);
  
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_sections_count_callback,
    .get_cell_height = PBL_IF_ROUND_ELSE(get_cell_height_callback, NULL),
    .draw_row = draw_row_handler,
    //.select_click = select_callback
  });
  
  // Move manu to next prayer time
  MenuIndex m_i = {0, next_prayer_time + 1};
  menu_layer_set_selected_index(s_menu_layer, m_i, MenuRowAlignCenter , false);
  
  menu_layer_set_click_config_onto_window(s_menu_layer,	window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  s_error_text_layer = text_layer_create((GRect) { .origin = {0, 44}, .size = {bounds.size.w, 60}});
  text_layer_set_text(s_error_text_layer, "Cannot\nschedule");
  text_layer_set_text_alignment(s_error_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_error_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(s_error_text_layer, GColorWhite);
  text_layer_set_background_color(s_error_text_layer, GColorBlack);
  layer_set_hidden(text_layer_get_layer(s_error_text_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_error_text_layer));
  
  s_status_text_layer = text_layer_create((GRect) { .origin = {1, 5}, .size = {bounds.size.w, 15}});
  snprintf(s_time_text, sizeof(s_time_text), "%02d:%02d", tm_.tm_hour, tm_.tm_min);
  text_layer_set_text(s_status_text_layer, s_time_text);
  text_layer_set_text_alignment(s_status_text_layer, GTextAlignmentCenter);
  text_layer_set_font(s_status_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(s_status_text_layer, GColorBlack);
  layer_add_child(window_layer, text_layer_get_layer(s_status_text_layer));
}

///--------------------------------------------------------------------------------------------------///
static void menu_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_error_text_layer);
}

///--------------------------------------------------------------------------------------------------///
static void timer_handler(void *data) {
  if (s_wakeup_timestamp == 0) {
    // get the wakeup timestamp for showing a countdown
    wakeup_query(s_wakeup_id, &s_wakeup_timestamp);
  }
  int countdown = s_wakeup_timestamp - time(NULL);
  snprintf(s_countdown_text, sizeof(s_countdown_text), "%d seconds", countdown);
  layer_mark_dirty(text_layer_get_layer(s_countdown_text_layer));
  app_timer_register(1000, timer_handler, data);
}

///--------------------------------------------------------------------------------------------------///
static void countdown_back_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop_all(true); // Exit app while waiting for tea to brew
}

///--------------------------------------------------------------------------------------------------///
// Cancel the current wakeup event on the countdown screen
static void countdown_cancel_handler(ClickRecognizerRef recognizer, void *context) {
  wakeup_cancel(s_wakeup_id);
  s_wakeup_id = -1;
  persist_delete(PERSIST_WAKEUP);
  window_stack_pop(true); // Go back to tea selection window
}

///--------------------------------------------------------------------------------------------------///
static void countdown_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, countdown_back_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, countdown_cancel_handler);
}

///--------------------------------------------------------------------------------------------------///
static void countdown_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_click_config_provider(window, countdown_click_config_provider);

  s_salah_text_layer = text_layer_create(GRect(0, 32, bounds.size.w, 20));
  text_layer_set_text(s_salah_text_layer, "Steeping time left");
  text_layer_set_text_alignment(s_salah_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_salah_text_layer));

  s_countdown_text_layer = text_layer_create(GRect(0, 72, bounds.size.w, 20));
  text_layer_set_text(s_countdown_text_layer, s_countdown_text);
  text_layer_set_text_alignment(s_countdown_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_countdown_text_layer));

  // Place a cancel "X" next to the bottom button to cancel wakeup timer
  s_cancel_text_layer = text_layer_create(GRect(124, 116, 24, 28));
  text_layer_set_text(s_cancel_text_layer, "X");
  text_layer_set_font(s_cancel_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_cancel_text_layer, GTextAlignmentLeft);
  layer_add_child(window_layer, text_layer_get_layer(s_cancel_text_layer));

  s_wakeup_timestamp = 0;
  app_timer_register(0, timer_handler, NULL);
}

///--------------------------------------------------------------------------------------------------///
static void countdown_window_unload(Window *window) {
  text_layer_destroy(s_countdown_text_layer);
  text_layer_destroy(s_cancel_text_layer);
  text_layer_destroy(s_salah_text_layer);
}

///--------------------------------------------------------------------------------------------------///
static void wakeup_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Exit app after tea is done
  window_stack_pop_all(true);
}

///--------------------------------------------------------------------------------------------------///
static void wakeup_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, wakeup_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, wakeup_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, wakeup_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, wakeup_click_handler);
}

///--------------------------------------------------------------------------------------------------///
static void wakeup_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_click_config_provider(window, wakeup_click_config_provider);

  // Bitmap layer for wakeup "tea is ready" image
  s_bitmap_layer = bitmap_layer_create(bounds);
  s_tea_bitmap = gbitmap_create_with_resource(RESOURCE_ID_TEA_SIGN);
  bitmap_layer_set_bitmap(s_bitmap_layer, s_tea_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_bitmap_layer));
}

///--------------------------------------------------------------------------------------------------///
static void wakeup_window_unload(Window *window) {
  gbitmap_destroy(s_tea_bitmap);
  bitmap_layer_destroy(s_bitmap_layer);
}

///--------------------------------------------------------------------------------------------------///
static void wakeup_handler(WakeupId id, int32_t reason) {
  //Delete persistent storage value
  persist_delete(PERSIST_WAKEUP);
  window_stack_push(s_wakeup_window, false);
  vibes_double_pulse();
}

///--------------------------------------------------------------------------------------------------///
static void init(void) {
  bool wakeup_scheduled = false;

  read_current_day();

  // Check if we have already scheduled a wakeup event
  // so we can transition to the countdown window
  if (persist_exists(PERSIST_WAKEUP)) {
    s_wakeup_id = persist_read_int(PERSIST_WAKEUP);
    // query if event is still valid, otherwise delete
    if (wakeup_query(s_wakeup_id, NULL)) {
      wakeup_scheduled = true;
    } else {
      persist_delete(PERSIST_WAKEUP);
      s_wakeup_id = -1;
    }
  }

  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers){
    .load = menu_window_load,
    .unload = menu_window_unload,
  });

  s_countdown_window = window_create();
  window_set_window_handlers(s_countdown_window, (WindowHandlers){
    .load = countdown_window_load,
    .unload = countdown_window_unload,
  });

  s_wakeup_window = window_create();
  window_set_window_handlers(s_wakeup_window, (WindowHandlers){
    .load = wakeup_window_load,
    .unload = wakeup_window_unload,
  });

  // Check to see if we were launched by a wakeup event
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    // If woken by wakeup event, get the event display "tea is ready"
    WakeupId id = 0;
    int32_t reason = 0;
    if (wakeup_get_launch_event(&id, &reason)) {
      wakeup_handler(id, reason);
    }
  } else if (wakeup_scheduled) {
    window_stack_push(s_countdown_window, false);
  } else {
    window_stack_push(s_menu_window, false);
  }

  // subscribe to wakeup service to get wakeup events while app is running
  wakeup_service_subscribe(wakeup_handler);
}

///--------------------------------------------------------------------------------------------------///
static void deinit(void) {
  window_destroy(s_menu_window);
}