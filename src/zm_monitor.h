//
// ZoneMinder Monitor Class Interfaces, $Date$, $Revision$
// Copyright (C) 2003  Philip Coombes
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
// 

#ifndef ZM_MONITOR_H
#define ZM_MONITOR_H

#include <sys/time.h>

static struct timezone dummy_tz; // To avoid declaring pointless one each time we use gettimeofday

#include "zm_coord.h"
#include "zm_image.h"
#include "zm_zone.h"
#include "zm_camera.h"

//
// This is the main class for monitors. Each monitor is associated
// with a camera and is effectivaly a collector for events.
//
class Monitor
{
public:
	typedef enum
	{
		NONE=1,
		PASSIVE,
		ACTIVE,
		X10
	} Function;

	typedef enum { IDLE, ALARM, ALERT } State;

protected:
	// These are read from the DB and thereafter remain unchanged
	int		id;
	char	*name;
	char	label_format[64];	// The format of the timestamp on the images
	Coord	label_coord;		// The coordinates of the timestamp on the images
	int		warmup_count;		// How many images to process before looking for events
	int		pre_event_count;	// How many images to hold and prepend to an alarm event
	int		post_event_count;	// How many unalarmed images must occur before the alarm state is reset
	int		alarm_frame_count;	// How many alarm frames we must get before we actually do anything about it.
	int		image_buffer_count; // Size of circular image buffer, at least twice the size of the pre_event_count
	int		fps_report_interval;// How many images should be captured/processed between reporting the current FPS
	int		ref_blend_perc;		// Percentage of new image going into reference image.

	Function	function;
	double	fps;
	Image	image;
	Image	ref_image;
	int		event_count;
	int		image_count;
	int		first_alarm_count;
	int		last_alarm_count;
	int		buffer_count;
	State state;
	int		n_zones;
	Zone	**zones;
	Event	*event;
	time_t	start_time;
	time_t	last_fps_time;
	int		shmid;

	typedef struct Snapshot
	{
		struct timeval	*timestamp;
		Image	*image;
	};

	Snapshot *image_buffer;

	typedef struct
	{
		State state;
		int last_write_index;
		int last_read_index;
		int last_event;
		bool forced_alarm;
		struct timeval *timestamps;
		unsigned char *images;
	} SharedImages;

	SharedImages *shared_images;

	bool record_event_stats;

	Camera *camera;
	
public:
	Monitor( int p_id, char *p_name, int p_function, int p_device, int p_channel, int p_format, int p_width, int p_height, int p_colours, bool p_capture, char *p_label_format, const Coord &p_label_coord, int p_warmup_count, int p_pre_event_count, int p_post_event_count, int p_alarm_frame_count, int p_image_buffer_count, int p_fps_report_interval, int p_ref_blend_perc, int p_n_zones=0, Zone *p_zones[]=0 );
	Monitor( int p_id, char *p_name, int p_function, const char *host, const char *port, const char *path, int p_width, int p_height, int p_colours, bool p_capture, char *p_label_format, const Coord &p_label_coord, int p_warmup_count, int p_pre_event_count, int p_post_event_count, int p_alarm_frame_count, int p_image_buffer_count, int p_fps_report_interval, int p_ref_blend_perc, int p_n_zones=0, Zone *p_zones[]=0 );
	~Monitor();

	void AddZones( int p_n_zones, Zone *p_zones[] );

	inline int Id() const
	{
		return( id );
	}
	inline char *Name() const
	{
		return( name );
	}
	State GetState() const;
	int GetImage( int index=-1 ) const;
	struct timeval GetTimestamp( int index=-1 ) const;
	unsigned int GetLastReadIndex() const;
	unsigned int GetLastWriteIndex() const;
	unsigned int GetLastEvent() const;
	double GetFPS() const;
	void ForceAlarm();
	void CancelAlarm();

	bool DumpSettings( char *output, bool verbose );
	void DumpZoneImage();

	unsigned int CameraWidth() const { return( camera->Width() ); }
	unsigned int CameraHeight() const { return( camera->Height() ); }
	inline int Capture()
	{
		if ( camera->PreCapture() == 0 )
		{
			if ( camera->PostCapture() == 0 )
			{
				return( 0 );
			}
		}
		return( -1 );
	}
	inline int PreCapture()
	{
		return( camera->PreCapture() );
	}
	inline int PostCapture()
	{
		if ( camera->PostCapture( image ) == 0 )
		{
			char label_time_text[64];
			char label_text[64];
			time_t now = time( 0 );

			if ( label_format[0] )
			{
				strftime( label_time_text, sizeof(label_time_text), label_format, localtime( &now ) );
				sprintf( label_text, label_time_text, name );

				image.Annotate( label_text, label_coord );
			}

			int index = image_count%image_buffer_count;

			if ( index == shared_images->last_read_index )
			{
				Warning(( "Buffer overrun at index %d\n", index ));
			}
			gettimeofday( image_buffer[index].timestamp, &dummy_tz );
			image_buffer[index].image->CopyBuffer( image );
			//memcpy( image_buffer[index].image->buffer, image.buffer, image.size );
			//Info(( "%d: %x - %x", index, image_buffer[index].image, image_buffer[index].image->buffer ));

			shared_images->last_write_index = index;

			image_count++;

			if ( image_count && !(image_count%fps_report_interval) )
			{
				fps = double(fps_report_interval)/(now-last_fps_time);
				Info(( "%s: %d - Capturing at %.2f fps\n", name, image_count, fps ));
				last_fps_time = now;
			}
			return( 0 );
		}
		return( -1 );
	}

	inline bool Ready()
	{
		return( function >= ACTIVE && image_count > warmup_count );
	}
 
	void DumpImage( Image *image ) const;
	bool Analyse();

	void Adjust( double ratio )
	{
		ref_image.Blend( image, 0.1 );
	}

	unsigned int Compare( const Image &image );
	void ReloadZones();
	static int Load( int device, Monitor **&monitors, bool capture=true );
	static int Load( const char *host, const char*port, const char*path, Monitor **&monitors, bool capture=true );
	static Monitor *Load( int id, bool load_zones=false );
	void StreamImages( unsigned long idle=5000, unsigned long refresh=50, FILE *fd=stdout, unsigned int ttl=0 );
};

#endif // ZM_MONITOR_H
