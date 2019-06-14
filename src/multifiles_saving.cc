#define DEBUG

#include<gst/gst.h>
#include<glib/gprintf.h> //for g_strdup_printf ()
#include<ctime>
#include<cstdlib>//for exit()
#include<iostream>
#include<cstdio>
#include<chrono>
#include"Utils.hh"
#include <gst/base/gstbaseparse.h> //for gst_base_parse_set_pts_interpolation ()
#include <boost/core/ignore_unused.hpp>
#include <unistd.h>

const int FILES_NUMBER = 12;

//Structure to contain all our information, so we can pass it to callbacks 
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *h264_depay;
  GstElement *h264_parse;
  GstElement *splitmuxsink;
  GMainLoop *loop;
} CustomData;

//static std::string filenames[FILES_NUMBER] = {std::string()};
static char *filenames[FILES_NUMBER] = {new char[30](), new char[30](), new char[30](), new char[30](),
					new char[30](), new char[30](), new char[30](), new char[30](),
					new char[30](), new char[30](), new char[30](), new char[30]()};

static const char *rtsp_url = "rtsp://192.168.4.12:8554/test";
static const char *filename = "cam02";
static  guint64 max_size_time = 3000000000;

static GOptionEntry entries[] = {
  { "rtsp_url", 'r', 0, G_OPTION_ARG_STRING, &rtsp_url, "Rtsp server to connect to", "URL" },
  { "filename", 'f', 0, G_OPTION_ARG_STRING, &filename, "Filename", "ID" },
  { "max_size_time", 'm', 0, G_OPTION_ARG_INT64, &max_size_time, "max_size_time", "max_size_time" },
  { NULL },
};
  
// Handler for the pad-added signal 
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

//handler for formatting properly the file saved
static gchar* formatted_file_saving_handler(GstChildProxy *splitmux, guint fragment_id);
					    
// gpointer is a void
static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data);

int main(int argc, char **argv){
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new ("- multifiles_saving-pipeline");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  //parsing command line
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }
  

  CustomData data;
  GstBus *bus;
  GstStateChangeReturn ret;

  // Initialize GStreamer
  gst_init (&argc, &argv);

  // Create the elements
  data.source = gst_element_factory_make ("rtspsrc", "source");
  data.h264_depay = gst_element_factory_make("rtph264depay", "depay");
  data.h264_parse = gst_element_factory_make("h264parse", "parse");
  data.splitmuxsink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
  
  // Create the empty pipeline
  data.pipeline = gst_pipeline_new ("multifiles_saving-pipeline");


  if (!data.pipeline || !data.source ||
      !data.h264_depay || !data.h264_parse || !data.splitmuxsink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  //Build the pipeline. Note that we are NOT linking the source at this
  //point. We will do it later
  gst_bin_add_many (GST_BIN (data.pipeline),
		    data.source, data.h264_depay, data.h264_parse,
		    data.splitmuxsink,
		    NULL);

  if( !gst_element_link_many(data.h264_depay,
			     data.h264_parse,
			     data.splitmuxsink,
			     NULL) ){
    g_printerr ("Some elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  //Set the rtsp location and the latency in one step
  g_object_set (data.source, "location", rtsp_url, "latency", 0, NULL);

  //this call is necessary because of a bug: sometime the mp4mux can not multiplex
  //the stream when onsecutive buffers have the same timestamp.
  //This can occur if h264parse receives a frame with invalid timestamp so it then guesses a timestamp.
  //this way the pts is computed  
  gst_base_parse_set_pts_interpolation( (GstBaseParse*)data.h264_parse, true );
  
  //GstElement *muxer = gst_element_factory_make("matroskamux", "matroskamux");
  //cyclically save 5 video seconds 12 times. values lower than 5 seconds cause a segmentation fault
  g_print("max size time: %" G_GUINT64_FORMAT "\n", max_size_time);
  g_object_set (data.splitmuxsink, "location", "video%02d.mp4",
		"max-size-time", max_size_time,
		//"max-size-bytes", 1000000,
		"max-files", FILES_NUMBER,
		//"muxer", muxer,
		"send-keyframe-requests", true, 
		NULL);

  // Connect to the pad-added signal 
  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);
  g_signal_connect( data.splitmuxsink, "format-location", G_CALLBACK (formatted_file_saving_handler), NULL);

  // Start playing 
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  // Listen to the bus 
  bus = gst_element_get_bus (data.pipeline);
  // GMainLoop *loop;
  guint bus_watch_id;
  //let's create a GMainLoop structure:
  //NULL as first parameter means that the default context will be used
  data.loop = g_main_loop_new (NULL, FALSE);
  bus_watch_id = gst_bus_add_watch (bus, bus_call, &data);
  g_print ("Running...\n");
  //the main thread stops it till g_main_loop_quit() is called
  //when this event occurs the g_main_loop_run returns
  g_main_loop_run (data.loop);

  //Free resources 
  gst_object_unref (bus);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  g_source_remove (bus_watch_id);
  //deallocating filenames
  for(int i = 0; i < FILES_NUMBER; ++i)
    delete[] filenames[i];
  return 0;

  //when there is an error trying to reconnect
  // //wating for 10 seconds
  // g_print("[multifiles_saving::main]. let's wait 10 seconds...\n");
  // usleep(10 * 1000000);
  // g_print("[multifiles_saving::main]. setting state...\n");
  // ret = gst_element_set_state (data.pipeline, GST_STATE_NULL);
  // if (ret == GST_STATE_CHANGE_FAILURE) 
  //   g_printerr ("[multifiles_saving::main]. Unable to set the pipeline to the null state.\n");
  // else{
  //   g_printerr ("[multifiles_saving::main]. changing state in null succeeded.\n");
  //   g_printerr("[multifiles_saving::main]. restoring format-location signal\n");
  //   g_signal_connect( data.splitmuxsink, "format-location", G_CALLBACK (formatted_file_saving_handler), NULL);
  // }
  // g_print("trying to reset the pipeline in playing state\n");
  // ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  // if (ret == GST_STATE_CHANGE_FAILURE) 
  //   g_printerr ("[multifiles_saving::main]. Unable to set the pipeline to the playing state.\n");
  // else
  //   g_printerr ("[multifiles_saving::main]. change state in play succeeded.\n");
  // g_main_loop_run(data.loop);
  // // Free resources 
  // gst_object_unref (bus);
  // gst_element_set_state (data.pipeline, GST_STATE_NULL);
  // gst_object_unref (data.pipeline);
  // g_source_remove (bus_watch_id);
  // //deallocating filenames
  // for(int i = 0; i < FILES_NUMBER; ++i)
  //   delete[] filenames[i];
  // return 0;
}


// This function will be called by the pad-added signal 
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *h264_depay_sink_pad = gst_element_get_static_pad (data->h264_depay, "sink");

  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  // If our audio and video converter are already linked, we have nothing to do here
  if(gst_pad_is_linked(h264_depay_sink_pad)){
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  // Check the new pad's type 
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  // Caps (capabilities) are composed of an array of GstStructure
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  // Each structure has a name and a set of property
  new_pad_type = gst_structure_get_name (new_pad_struct);
  g_print ("Pad received is of type: '%s'\n", new_pad_type);
  // the name is important to understand what sink cap we need to link the element
  if( g_str_has_prefix (new_pad_type, "application/x-rtp") ) {
    g_print("trying to link the rtpsrc' src and h264depay's sink\n");
    // Attempt the link 
    ret = gst_pad_link_full (new_pad, h264_depay_sink_pad, GST_PAD_LINK_CHECK_CAPS);
    if (GST_PAD_LINK_FAILED (ret)) {
      g_print ("Type is '%s' but link failed.\n", new_pad_type);
    } else {
      g_print ("Link succeeded (type '%s').\n", new_pad_type);
    }
  }

 exit:
  // Unreference the new pad's caps, if we got them 
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  // Unreference the sink pad 
  gst_object_unref(h264_depay_sink_pad);
}


gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data){
  //GMainLoop *loop = (GMainLoop *)data;
  boost::ignore_unused(bus);
  CustomData *CustomData_ptr = (CustomData *)data;
  GMainLoop *loop = CustomData_ptr->loop;
  //g_print( "got message %s %s \n", GST_MESSAGE_TYPE_NAME(msg), gst_structure_get_name ( gst_message_get_structure(msg) ) );

  switch (GST_MESSAGE_TYPE (msg)) {

  case GST_MESSAGE_EOS:
    g_print ("[multifiles_saving::bus_call]. End of stream\n");
    //g_main_loop_quit (loop);
    break;

  case GST_MESSAGE_ERROR: {
    gchar  *debug;
    GError *error;
    // static bool trying_to_reconnect = false;

    // if(!trying_to_reconnect){
    //   trying_to_reconnect = true;
    //   gst_message_parse_error (msg, &error, &debug);
    //   g_free (debug);

    //   g_printerr ("[multifiles_saving::bus_call]. Error: %s\n", error->message);
    //   g_error_free (error);

    //   //g_main_loop_quit (loop);
    //   gst_element_set_state (CustomData_ptr->pipeline, GST_STATE_READY);
    //   //wating for 30 seconds
    //   g_print("[multifiles_saving::bus_call]. let's wait 10 seconds to give time to reconnection...");
    //   usleep(10 * 1000000);
    //   g_print("trying to reset the pipeline in playing state\n");
    //   GstStateChangeReturn ret = gst_element_set_state (CustomData_ptr->pipeline, GST_STATE_PLAYING);
    //   if (ret == GST_STATE_CHANGE_FAILURE) 
    // 	g_printerr ("[multifiles_saving::bus_call]. Unable to set the pipeline to the playing state.\n");
    //   else{
    // 	g_printerr ("[multifiles_saving::bus_call]. change state succeeded.\n");
    // 	trying_to_reconnect = false;
    //   }
      
    // }else{
    //   gst_message_parse_error (msg, &error, &debug);
    //   g_free (debug);
    //   g_error_free (error);
    // }


    gst_message_parse_error (msg, &error, &debug);
    g_free (debug);
    
    g_printerr ("[multifiles_saving::bus_call]. Error: %s\n", error->message);
    g_error_free (error);
    
    //g_main_loop_quit (loop);
    
    //this code snippet work
    //wating for 10 seconds
    g_print("[multifiles_saving::bus_call]. let's wait 10 seconds...\n");
    usleep(10 * 1000000);
    //if put ready, after reconnection it starts from the last fragment-id
    //if put null,  after reconnection it starts from the last fragment-id
    gst_element_send_event(CustomData_ptr->pipeline, gst_event_new_eos());
    usleep(1 * 1000000);
    g_print("[multifiles_saving::bus_call]. setting state...\n");
    GstStateChangeReturn ret = gst_element_set_state (CustomData_ptr->pipeline, GST_STATE_NULL);
    if (ret == GST_STATE_CHANGE_FAILURE) 
      g_printerr ("[multifiles_saving::bus_call]. Unable to set the pipeline to the NULL state.\n");
    else{
      g_printerr ("[multifiles_saving::bus_call]. changing state in ready succeeded.\n");
      g_printerr("[multifiles_saving::bus_call]. restoring format-location signal\n");
      //GstPad *split_sink= gst_element_get_static_pad (CustomData_ptr->splitmuxsink, "sink");
      //GstPadTemplate *templ = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(CustomData_ptr->splitmuxsink), "video");
      //GstPad *split_pad = gst_element_request_pad(CustomData_ptr->splitmuxsink, templ, NULL, NULL);
      GstPad *split_pad = gst_element_get_static_pad(CustomData_ptr->splitmuxsink,"video");
      GstPad *h264_src = gst_element_get_static_pad (CustomData_ptr->h264_parse, "src");
      gst_pad_unlink (h264_src, split_pad);
      gst_object_unref(CustomData_ptr->splitmuxsink);
      gst_bin_remove(GST_BIN (CustomData_ptr->pipeline), CustomData_ptr->splitmuxsink);      
      CustomData_ptr->splitmuxsink = gst_element_factory_make("splitmuxsink", "splitmuxsink");
      gst_bin_add(GST_BIN(CustomData_ptr->pipeline), CustomData_ptr->splitmuxsink);
      if( !gst_element_link(CustomData_ptr->h264_parse, CustomData_ptr->splitmuxsink) )
	g_printerr ("parse and split nont linked");
      g_signal_connect( CustomData_ptr->splitmuxsink, "format-location", G_CALLBACK (formatted_file_saving_handler), NULL);
    }
    g_print("trying to reset the pipeline in playing state\n");
    ret = gst_element_set_state (CustomData_ptr->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) 
      g_printerr ("[multifiles_saving::bus_call]. Unable to set the pipeline to the playing state.\n");
    else{
      g_printerr ("[multifiles_saving::bus_call]. change state in play succeeded.\n");
      g_main_loop_run (CustomData_ptr->loop);
    }
    break;
  }
  case GST_MESSAGE_ELEMENT: {
    //g_print("Received a gst_message_element\n");
    break;
  }
  default:
    break;
  }
  
  return true;
}

gchar* formatted_file_saving_handler(GstChildProxy *splitmux, guint fragment_id){
  boost::ignore_unused(splitmux);
  //D( g_print("[formatted_file_saving_handler]. fragment_id: %d\n", fragment_id) );
  D( Time_spent<>() );  
  // time_t rawtime;
  // struct tm * timeinfo;
  // char filename_buffer[30];
  
  // time (&rawtime);
  // timeinfo = localtime (&rawtime);

  // //making time string in the format YYYY-mm-dd_hh:mm:ss
  // strftime (filename_buffer, 30, "%F_%T", timeinfo);

  // //making filename string in the forma YYYY-mm-dd_hh:mm:ss_filename_fragment_id
  // sprintf(filename_buffer, "%s_%s_%d.mp4", filename_buffer, filename, fragment_id);

  // //if int the filenames array at the fragment_id position we already had
  // //a filename we must delete it
  // if( !( (filenames[fragment_id]).empty() ) )
  //   if( remove( (filenames[fragment_id] ).c_str() ) ) 
  //     perror( "Error deleting file" );

  // filenames[fragment_id] = filename_buffer;

  std::time_t now;
  struct tm *timeinfo;
  
  std::time (&now);
  timeinfo = std::localtime (&now);

  //if int the filenames array at the fragment_id position we already had
  //a filename we must delete it
  if( *filenames[fragment_id] )
    if( remove( (filenames[fragment_id] ) ) ) 
      perror( "Error deleting file" );

  //making time string in the format YYYY-mm-dd_hh:mm:ss
  strftime (filenames[fragment_id], 30, "%F_%T", timeinfo);

  //making filename string in the forma YYYY-mm-dd_hh:mm:ss_filename_fragment_id
  sprintf( filenames[fragment_id], "%s_%s_%d.mp4", filenames[fragment_id], filename, fragment_id );

  D( std::cout << filenames[fragment_id] << std::endl );
  //this function allocates the memory to hold the result.
  //The returned string should be freed with g_free() when no longer needed
  //This should be done by the function caller, I guess
  return g_strdup_printf("%s", filenames[fragment_id] );
}
