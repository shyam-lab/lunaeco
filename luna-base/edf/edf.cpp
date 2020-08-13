

//    --------------------------------------------------------------------
//
//    This file is part of Luna.
//
//    LUNA is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Luna is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Luna. If not, see <http://www.gnu.org/licenses/>.
//
//    Please see LICENSE.txt for more details.
//
//    --------------------------------------------------------------------

#include "edf.h"
#include "defs/defs.h"
#include "helper/helper.h"
#include "helper/logger.h"
#include "miscmath/miscmath.h"
#include "db/db.h"
#include "edfz/edfz.h"
#include "dsp/resample.h"

#include "slice.h"
#include "tal.h"
#include "eval.h"
#include "clocs/clocs.h"
#include "timeline/timeline.h"

//#include <ftw.h>

#include <iostream>
#include <fstream>

extern writer_t writer;
extern logger_t logger;

void writestring( const std::string & s , int n , FILE * file )
{
  std::string c = s;
  c.resize(n,' ');
  fwrite( c.data() , 1 , n , file );
}

void writestring( const int & s , int n , FILE * file )
{
  std::string c = Helper::int2str(s);
  c.resize(n,' ');
  fwrite( c.data() , 1 , n , file );
}

void writestring( const double & s , int n , FILE * file )
{
  std::string c = Helper::dbl2str_fixed(s,n);
  c.resize(n,' ');
  fwrite( c.data() , 1 , n , file );
}

edf_t::endian_t edf_t::endian = edf_t::MACHINE_LITTLE_ENDIAN;

uint64_t edf_t::get_filesize(FILE *file)
{  
  uint64_t lCurPos, lEndPos;
  lCurPos = ftell(file);
  fseek(file, 0, 2);
  lEndPos = ftell(file);
  fseek(file, lCurPos, 0);
  return lEndPos;
}

int edf_t::get_int( byte_t ** p , int sz )
{
  std::string s = edf_t::get_string( p , sz );
  int t = 0;
  if ( ! Helper::str2int( s , &t ) ) 
    Helper::halt( "problem converting to an integer value: [" + s + "]"  );
  return t;
}

double edf_t::get_double( byte_t ** p , int sz )
{
  std::string s = edf_t::get_string( p , sz );

  double t = 0;
  if ( s == "" ) return -1;
  
  if ( ! Helper::from_string<double>( t , s , std::dec ) ) 
    {     
      logger << "returning -1: [" << s << "] is not a valid real number\n";
      return -1;
    }
  return t;
}

std::string edf_t::get_string( byte_t ** p , int sz )
{
  // only US-ASCII printable characters allowed: 32 .. 126 
  // other characters mapped to '?'
  std::vector<char> buf(sz+1);
  for (int i=0;i<sz;i++)
    {
      buf[i] = **p;
      if ( buf[i] < 32 || buf[i] > 126 ) buf[i] = 63; // '?'
      ++(*p);      
    }
  buf[sz] = '\0';
  std::string str = &buf[0];
  // trim trailing whitespace 
  // (when writing header back out, we expand whitespace to desired length)
  Helper::rtrim(str);
  return str;
}

void edf_t::skip( byte_t ** p , int sz ) 
{
  (*p) += sz;
}

std::vector<char> edf_t::get_bytes( byte_t ** p , int sz )
{
  std::vector<char> buf(sz);
  for (int i=0;i<sz;i++)
    {
      buf[i] = **p;
      ++(*p);
    }
  return buf;
}


inline double edf_record_t::dig2phys( int16_t d , double bv , double offset )
{
  return bv * ( offset + d ) ; 
}

inline int16_t edf_record_t::phys2dig( double d , double bv , double offset )
{
  return d / bv - offset ; 
}


inline int16_t edf_record_t::tc2dec( char a , char b )
{        
  union 
  {
    int16_t       one[1];    
    unsigned char two[2];      
  } buffer;    
  
  if ( edf_t::endian == edf_t::MACHINE_LITTLE_ENDIAN )
    {
      buffer.two[0] = a;
      buffer.two[1] = b;
    }
  else
    {
      buffer.two[0] = b;
      buffer.two[1] = a;
    }
  return buffer.one[0];   
}


inline void edf_record_t::dec2tc( int16_t x , char * a , char * b )
{
  union 
  {
    int16_t       one[1];    
    unsigned char two[2];      
  } buffer;    

  buffer.one[0] = x;
  
  if ( edf_t::endian == edf_t::MACHINE_LITTLE_ENDIAN )
    {
      *a = buffer.two[0];
      *b = buffer.two[1];
    }
  else
    {
      *b = buffer.two[0];
      *a = buffer.two[1];
    }
  
}



std::string edf_header_t::summary() const
{

  std::stringstream ss;

  ss << "Patient ID     : " << patient_id << "\n"
     << "Recording info : " << recording_info << "\n"
     << "Start date     : " << startdate << "\n"
     << "Start time     : " << starttime << "\n"
     << "\n"
     << "# signals      : " << ns << "\n"
     << "# records      : " << nr << "\n"
     << "Rec. dur. (s)  : " << record_duration << "\n\n";
  
  for (int s=0;s<ns;s++)
    {
      
      ss << "Signal " << (s+1) << " : [" << label[s] << "]\n";
      
      std::string primary = label[s];
      
      // is alias? ( will have been mapped already )
      if ( cmd_t::primary_alias.find( primary ) != cmd_t::primary_alias.end() )
	{
	  std::string aliases = Helper::stringize( cmd_t::primary_alias[ primary ] , " | " );
	  ss << "\taliased from         : " << aliases << "\n"; 
	}
      
      if ( is_annotation_channel( s ) ) 
	ss << "\tannotation channel\n";
      else
	ss << "\t# samples per record : " << n_samples[s] << "\n"	
	   << "\ttransducer type      : " << transducer_type[s] << "\n"
	   << "\tphysical dimension   : " << phys_dimension[s] << "\n"
	   << "\tmin/max (phys)       : " << physical_min[s] << "/" << physical_max[s] << "\n"
	   << "\tEDF min/max (phys)   : " << orig_physical_min[s] << "/" << orig_physical_max[s] << "\n"
	   << "\tmin/max (digital)    : " << digital_min[s] << "/" << digital_max[s] << "\n"
	   << "\tEDF min/max (digital): " << orig_digital_min[s] << "/" << orig_digital_max[s] << "\n"
	   << "\tpre-filtering        : " << prefiltering[s] << "\n\n";
      
    }

  return ss.str();
}

void edf_t::description( const param_t & param ) 
{

  signal_list_t signals = header.signal_list( param.requires( "sig" ) );
  
  bool channel_list = param.has( "channels" );
  
  if ( channel_list )
    {
      for (int s=0;s<signals.size();s++) 
	{
	  if ( header.is_data_channel( signals(s) ) )
	    std::cout << signals.label(s) << "\n";
	}
      return;
    }
    
  uint64_t duration_tp = globals::tp_1sec * (uint64_t)header.nr * header.record_duration ;

  int n_data_channels = 0 , n_annot_channels = 0;
  int n_data_channels_sel = 0 , n_annot_channels_sel = 0;
  
  for (int s=0;s<header.ns;s++) 
    {
      if ( header.is_data_channel(s) )
	++n_data_channels;
      else 
	++n_annot_channels;
    }

  for (int s=0;s<signals.size(); s++) 
    {
      if ( header.is_data_channel( signals(s) ) )
	++n_data_channels_sel;
      else 
	++n_annot_channels_sel;
    }

  clocktime_t et( header.starttime );
  if ( et.valid )
    {
      double time_hrs = ( timeline.last_time_point_tp * globals::tp_duration ) / 3600.0 ; 
      et.advance( time_hrs );
    }
  
  std::cout << "EDF filename      : " << filename << "\n"
	    << "ID                : " << id << "\n";

  if ( header.edfplus ) 
    std::cout << "Header start time : " << header.starttime << "\n"
	      << "Last observed time: " << et.as_string() << "\n";
  else 
    std::cout << "Clock time        : " << header.starttime << " - " << et.as_string() << "\n";

  std::cout << "Duration          : " << Helper::timestring( duration_tp ) << "\n";

  if ( n_data_channels_sel < n_data_channels )
    std::cout << "# signals         : " << n_data_channels_sel << " selected (of " << n_data_channels << ")\n";
  else
    std::cout << "# signals         : " << n_data_channels << "\n";
    
  if ( n_annot_channels > 0 )
    {
      if ( n_annot_channels_sel < n_annot_channels )
	std::cout << "# EDF annotations : " << n_annot_channels_sel << " selected (of " << n_annot_channels << ")\n";
      else
	std::cout << "# EDF annotations : " << n_annot_channels << "\n";
    }
  
  std::cout << "Signals           :";

  int cnt=0;
  for (int s=0;s<signals.size();s++) 
    {
      if ( header.is_data_channel( signals(s) ) )
	std::cout << " " 
		  << signals.label(s) 
		  << "[" << header.sampling_freq( signals(s) ) << "]";
      if ( ++cnt >= 6 ) { cnt=0; std::cout << "\n                   "; } 
    }
  std::cout << "\n\n";
  
  
}


void edf_t::report_aliases() const
{
  // annotations
  std::map<std::string,std::string>::const_iterator aa = timeline.annotations.aliasing.begin();
  while ( aa != timeline.annotations.aliasing.end() )
    {
      writer.level( aa->first , globals::annot_strat );
      writer.value( "ORIG" , aa->second );
      ++aa;
    }
  writer.unlevel( globals::annot_strat );
  
  // channels
  std::map<std::string,std::string>::const_iterator cc = header.aliasing.begin();
  while ( cc != header.aliasing.end() )
    {
      writer.level( cc->first , globals::signal_strat );
      writer.value( "ORIG" , cc->second );
      ++cc;
    }
  writer.unlevel( globals::signal_strat );

}

void edf_t::terse_summary( const bool write_signals ) const
{
  
  // variable definitions
  writer.var( "NS" , "Number of signals" );
  writer.var( "NR" , "Number of records" ); 
  writer.var( "REC.DUR" , "Record duration (sec)" );
  writer.var( "TOT.DUR.SEC" , "Total recording duration (sec)" );
  writer.var( "TOT.DUR.HMS" , "Total recording duration (hh:mm:ss)" );

  writer.var( "SR" , "Sampling race (points per second)" );
  writer.var( "PDIM" , "Physical dimension/units" );
  writer.var( "PMIN" , "Physical minimum" );
  writer.var( "PMAX" , "Physical maximum" );

  writer.var( "DMIN" , "Digital minimum" );
  writer.var( "DMAX" , "Digital maximum" );

  // write output
  writer.value( "NS" , header.ns );
  writer.value( "NR" , header.nr );
  writer.value( "REC.DUR" , header.record_duration );

  // total duration in TP units
  uint64_t duration_tp = globals::tp_1sec * (uint64_t)header.nr * header.record_duration ;
  std::string total_duration_hms = Helper::timestring( duration_tp );
  writer.value( "TOT.DUR.SEC" , header.nr * header.record_duration );
  writer.value( "TOT.DUR.HMS" , total_duration_hms );

  writer.value( "EDF_ID" , header.patient_id );
  writer.value( "START_TIME" , header.starttime );
  writer.value( "START_DATE" , header.startdate );

  if ( write_signals ) 
    writer.value( "SIGNALS" , Helper::stringize<std::vector<std::string> >( header.label ) ); 

  for (int s=0;s<header.ns;s++)
    {
      // channel name
      writer.level( header.label[s] , globals::signal_strat );

      // channel type
      writer.value( "TYPE" , globals::map_channel_label(  header.label[s]  )  );
      
      // number of samples
      writer.value( "SR" , header.n_samples[s] / (double)header.record_duration );
      
      // physical dimension
      writer.value( "PDIM" , header.phys_dimension[s] );

      // physical min/max
      writer.value( "PMIN" , header.physical_min[s] );
      writer.value( "PMAX" , header.physical_max[s] );
      
      // digital min/max
      writer.value( "DMIN" , header.digital_min[s] );
      writer.value( "DMAX" , header.digital_max[s] );

    }
  
  writer.unlevel( globals::signal_strat );

}




std::set<int> edf_header_t::read( FILE * file , edfz_t * edfz , const std::set<std::string> * inp_signals )
{

  // must be *either* EDF or EDFZ
  
  if ( file != NULL && edfz != NULL ) 
    Helper::halt( "internal error in edf_header_t::read(), unclear whether EDF or EDFZ" );
  
  // Fixed buffer size for header
  // Total header = 256 + ns*256
 
  const int hdrSz = 256; 
  
  // Allocate space in the buffer for the header only
  byte_t * q = new byte_t[ hdrSz ];
  byte_t * q0 = q;

  //
  // Read start of header into the buffer
  //

  size_t rdsz;

  if ( file ) {
    rdsz = fread( q , 1, hdrSz , file);
  } else {
    rdsz = edfz->read( q , hdrSz );
  }
    
  
  std::set<int> channels;
  
  version        = edf_t::get_string( &q , 8 );
  patient_id     = edf_t::get_string( &q , 80 );
  recording_info = edf_t::get_string( &q , 80 );
  startdate      = edf_t::get_string( &q , 8 );
  starttime      = edf_t::get_string( &q , 8 );
  nbytes_header  = edf_t::get_int( &q , 8 );  
  reserved       = edf_t::get_bytes( &q , 44 );

  // enforce check that reserevd field contains only US-ASCII characters 32-126
  // not clear this is needed, but other software seems to prefer this

  Helper::ascii7( &reserved , ' ' );

  //
  // ensure starttime is in the PM, i.e. 07:00 --> 19:00
  // unless we've otherwise been instructed to respect
  // AM start-times (assume-pm-start=0);  but going to bed at midnight or 
  // 1am should be fine... so 

  //    6am ....  12pm   .... 6pm .... 12am .... 6am 
  //                    |4pm     
  
  //
  // assumes typical sleep onset 
  //
  
  if ( globals::assume_pm_starttime )
    {
      clocktime_t st( starttime );
      if ( st.valid ) 
	{
	  if      ( st.h >= globals::assume_pm_starttime_hour && st.h < 12 ) st.h += 12;
	  else if ( st.h == 12 ) st.h = 0; 
	  starttime = st.as_string();	  
	}
    }

  
  // EDF+C  continuous EDF 
  // EDF+D  discontinuous EDF+
  
  if (    reserved[0] == 'E' 
       && reserved[1] == 'D' 
       && reserved[2] == 'F'
       && reserved[3] == '+' )
    {
      
      if ( reserved[4] == 'C' )
	{
	  edfplus = true;
	  continuous = true;
	}
      else if ( reserved[4] == 'D' )
	{
	  edfplus = true;
	  continuous = false;
	}
    }
  else
    {
      edfplus = false;
      continuous = true;
    }
  
  // check whether we are forcing EDF format
  if ( globals::force_edf )
    {
      logger << "  forcing read as EDF [else remove force-edf=1]\n";

      edfplus = false;
      continuous = true;  
      reserved[0] = ' '; 
      reserved[1] = ' ';
      reserved[2] = ' ';
      reserved[3] = ' ';
      reserved[4] = ' ';

    }

  // Number and direction of records/signals
  
  nr                   = edf_t::get_int( &q , 8 );

  // store copy as 'original file' (i.e. if the edf_t is restructured, then
  // nr will be smaller, but we need the same nr_all for remaining file access

  nr_all               = nr; 

  record_duration      = edf_t::get_double( &q , 8 );

  record_duration_tp   = record_duration * globals::tp_1sec;

  ns_all               = edf_t::get_int( &q , 4 );


  // Free buffer
  delete [] q0;


  //
  // Per-signal header information
  //

  
  // read next 256 bytes per signal, i.e. overwriting existing buffer
  byte_t * p = new byte_t[ hdrSz * ns_all ]; 
  byte_t * p0 = p;

  if ( file ) 
    rdsz = fread( p , 1, hdrSz * ns_all , file);      
  else
    rdsz = edfz->read( p , hdrSz * ns_all );

  // for each of 'ns_all' signals
  
  ns = 0; // actual number of important signals

  std::vector<std::string> tlabels;
  std::set<std::string> slabels;
  
  for (int s=0;s<ns_all;s++)
    {
      
      // signal label
      std::string l = Helper::trim( edf_t::get_string( &p , 16 ) );

      // trim spaces
      
      // remove spaces?
      if ( globals::replace_channel_spaces )
	l = Helper::search_replace( l , ' ' , globals::space_replacement );
	   
      // does this exist already? if so, uniqify 
      if ( slabels.find( l ) != slabels.end() )
	{
	  int inc = 1;
	  while ( 1 ) 
	    {
	      // new unique label?
	      if ( slabels.find( l + "." + Helper::int2str( inc )  ) == slabels.end() )
		{
		  logger << " uniqifying " << l ;
		  l = l + "." + Helper::int2str( inc );
		  logger << " to " << l << "\n";
		  break;
		}
	      else // keep trying
		++inc;
	    }
	}
      
      // store temporary
      tlabels.push_back( l );
      slabels.insert( l );

      // track original label position
      label_all[ l ] = s ;

    }
  
  // for each signal, does it match?
  // (and if so, change this to "standard" form)
  
  for (int s=0;s<ns_all;s++)
    {
      
      // retrieve temp label
      std::string l = tlabels[s];
      
      bool include = inp_signals == NULL || signal_list_t::match( inp_signals , &l , slabels );
      
      // imatch allows for case-insensitive match of 'edf annotation*'  (i.e. 14 chars)
      bool annotation = Helper::imatch( l , "EDF Annotation" , 14 ) ;

      // optionally skip all EDF annotation channels?
      if ( annotation && ( globals::skip_edf_annots || globals::force_edf ) ) 
	{	  
	  include = false;
	}

      if ( include ) 
	{
	  
	  channels.insert(s);
	  
	  annotation_channel.push_back( annotation );
	  
	  if ( annotation && ! edfplus ) 
	    {
	      //Helper::halt( "file must be EDF+ to support annotations" );
	      logger << " detected an annotation channel in EDF: will treat as EDF+\n";
	      edfplus = true;
	    }
	  
	  // first annotation channel is time-track
	  if ( annotation && t_track == -1 ) 
	    {
	      t_track = label.size(); 
	    }

	  // label mapping only to non-annotation channels
	  if ( ! annotation ) 
	    label2header[ l ] = label.size(); 
	  
	  label.push_back( l );	  
	  
	  ++ns;

	}
    }

  // transducer type
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() ) 
	transducer_type.push_back( edf_t::get_string( &p , 80 ) );
      else 
	edf_t::skip( &p , 80 );
    }

  // physical dimension
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() ) 
	{

	  phys_dimension.push_back( edf_t::get_string( &p , 8 ) );

	}
      else
	edf_t::skip( &p , 8 );
    }

  // physical min
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() ) 
	physical_min.push_back( edf_t::get_double( &p , 8 ) );
      else
	edf_t::skip( &p , 8 );
    }

  // physical max
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() )       
	physical_max.push_back( edf_t::get_double( &p , 8 ) );
      else
	edf_t::skip( &p , 8 );
    }

  // digital min  
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() )       
	digital_min.push_back( edf_t::get_int( &p , 8 ) );
      else
	edf_t::skip( &p , 8 );      
    }

  // digital max
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() )
	digital_max.push_back( edf_t::get_int( &p , 8 ) );
      else
	edf_t::skip( &p , 8 );      
    }


  // prefiltering information
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() )
	prefiltering.push_back( edf_t::get_string( &p , 80 ) );
      else
	edf_t::skip( &p , 80 );
    }
  
  // number of samples per record
  for (int s=0;s<ns_all;s++)
    {
      int x = edf_t::get_int( &p , 8 );
      if ( channels.find(s) != channels.end() )
	n_samples.push_back( x );      
      n_samples_all.push_back( x );
    }
  
  // reserved field
  for (int s=0;s<ns_all;s++)
    {
      if ( channels.find(s) != channels.end() )
	signal_reserved.push_back( edf_t::get_string( &p , 32 ) );
      else
	edf_t::skip( &p , 32 );
    }

  //
  // time-track absolute offset in record
  //

  if ( t_track != -1 )
    {
      t_track_edf_offset = 0;
      for (int ss=0;ss<t_track;ss++)
	t_track_edf_offset += 2 * n_samples_all[ss];	
    }

  //
  // derived values: note, here 'ns' not 'ns_all'
  //

  orig_physical_min = physical_min;
  orig_physical_max = physical_max;

  orig_digital_min = digital_min;
  orig_digital_max = digital_max;
  
  for (int s=0;s<ns;s++)
    {
      double bv = ( physical_max[s] - physical_min[s] ) / (double)( digital_max[s] - digital_min[s] ) ;
      bitvalue.push_back( bv );
      offset.push_back( ( physical_max[s] / bv ) - digital_max[s] ) ;
    }  
  

  // clean up buffer
  delete [] p0 ;

  // return mapping of imported channel numbers
  return channels;
  
}




bool edf_record_t::read( int r )
{
  
  // bound checking on 'r' already done, via edf_t::read_record();
  
  // skip if already loaded?
  if ( edf->loaded( r ) ) return false;
  
  // allocate space in the buffer for a single record, and read from file
  
  byte_t * p = new byte_t[ edf->record_size ];
  
  byte_t * p0 = p;

  // EDF?
  if ( edf->file ) 
    {
      
      // determine offset into EDF
      uint64_t offset = edf->header_size + (uint64_t)(edf->record_size) * r;
      
      // find the appropriate record
      fseek( edf->file , offset , SEEK_SET );
  
      // and read it
      size_t rdsz = fread( p , 1, edf->record_size , edf->file );
    }
  else // EDFZ
    {
      
      if ( ! edf->edfz->read_record( r , p , edf->record_size ) ) 
	Helper::halt( "corrupt .edfz or .idx" );      

    }

  // which signals/channels do we actually want to read?
  // header : 0..(ns-1)
  // from record data : 0..(ns_all-1), from which we pick the 'ns' entries is 'channels'
  // data[] is already created for 'ns' signals
  
  // for convenience, use name 'channels' below
  std::set<int> & channels = edf->inp_signals_n;
    
  int s = 0;
  
  for (int s0=0; s0<edf->header.ns_all; s0++)
    {
      
      // need to EDF-based header, i.e. if skipped signal still need size to skip
      const int nsamples = edf->header.n_samples_all[s0];
      
      //
      // skip this signal?
      //
      
      if ( channels.find( s0 ) == channels.end() )
	{
	  p += 2 * nsamples;
	  continue;
	}
      
      //
      // Data or annotation channel? (note: lookup is based on 's' not
      // 's0', i.w. loaded channels, not all EDF channels
      //
      
      bool annotation = edf->header.is_annotation_channel( s );
      
      //
      // s0 : actual signal in EDF
      // s  : where this signal will land in edf_t
      //
      
      if ( ! annotation ) 
	{
	  
	  for (int j=0; j < nsamples ; j++)
	    {
	  
	      //int d = tc2dec( **p ,  *((*p)+1)  ); 
	      int16_t d = tc2dec( *p ,  *(p+1)  ); 
	      
	      // advance pointer
	      p += 2;
	      
	      // store digital data-point
	      data[s][j] = d;
	      
	      // physically-scaled data-point	  
	      if ( false )
		if ( d < edf->header.orig_digital_min[s] || d > edf->header.orig_digital_max[s] ) 
		  {	
		    
		  std::cout << "OUT-OF-BOUNDS" << "\t"
			    << edf->id << "\t"
			    << "[" << globals::current_tag << "]\t"
			    << edf->header.label[s] << "\t"
			    << "digt: " << d << "\t"
			    << edf->header.orig_digital_min[s] << " .. " 
			    << edf->header.orig_digital_max[s] << "\t"
			    << "phys: " << edf->header.bitvalue[s] * ( edf->header.offset[s] + d ) << "\t"
			    << edf->header.orig_physical_min[s] << " .. " 
			    << edf->header.orig_physical_max[s] << "\n"; 

		  if (  d < edf->header.orig_digital_min[s] ) 
		    d = edf->header.orig_digital_min[s];
		  else 
		    d = edf->header.orig_digital_max[s];
		  
		  }
	      
	      // concert to physical scale
	      //pdata[s][j] = edf->header.bitvalue[s] * ( edf->header.offset[s] + d );
	      //pdata[s][j] = dig2phys( d , s ) ;
	      
	    }
	}
      else // read as a ANNOTATION
	{
	  
	  // Note, because for a normal signal, each sample takes 2 bytes,
	  // here we read twice the number of datapoints
	  
	  for (int j=0; j < 2 * nsamples; j++)
	    {
	      
	      // store digital data-point
	      data[s][j] = *p;
	      
	      // advance pointer
	      p++;
	      
	    }	  
	  
	}


      // next signal
      ++s;

    }
  
  //
  // Clean up
  //

  delete [] p0;
  
  return true;

}



bool edf_t::read_records( int r1 , int r2 )
{

  // This only tries to load records that are 'retained' and 
  // not already in memory

  if ( r1 < 0 ) r1 = 0;
  if ( r1 > header.nr_all ) r1 = header.nr_all - 1;
  
  if ( r2 < r1 ) r2 = r1;
  if ( r2 > header.nr_all ) r2 = header.nr_all - 1;

  //std::cerr << "edf_t::read_records :: scanning ... r1, r2 " << r1 << "\t" << r2 << "\n";
  
  for (int r=r1;r<=r2;r++)
    {

//       if ( ! timeline.retained(r) ) 
// 	std::cerr << "NOT retained " << r << " " << timeline.retained(r) << "\n";
      
      if ( timeline.retained(r) )
	{
	  if ( ! loaded( r ) ) 
	    {
	      edf_record_t record( this ); 
	      record.read( r );
	      records.insert( std::map<int,edf_record_t>::value_type( r , record ) );	      
	    }
	}
    }
  return true;
}


bool edf_t::read_from_ascii( const std::string & f , // filename
			     const std::string & i , // id
			     const int Fs , // fixed Fs for all signals
			     const std::vector<std::string> & labels0 ,  // if null, look for header
			     const std::string & startdate , 
			     const std::string & starttime 
			     ) 
{
  
  filename = Helper::expand( f );
  
  id = i;
  
  bool has_arg_labels = labels0.size() > 0;

  bool has_header_labels = false;

  std::vector<std::string> labels;

  if ( has_arg_labels ) labels = labels0;

  if ( ! Helper::fileExists( filename ) ) 
    Helper::halt( "could not read " + filename );

  bool compressed = Helper::file_extension( filename , "gz" );
  
  std::ifstream IN1( filename.c_str() , std::ios::in );

  gzifstream ZIN1;
  
  if ( compressed  ) 
    ZIN1.open( filename.c_str() );
  else
    IN1.open( filename.c_str() , std::ios::in );    
  
  std::string line;

  if ( compressed ) 
    {
      Helper::safe_getline( ZIN1 , line );
      if ( ZIN1.eof() || line == "" ) Helper::halt( "problem reading from " + filename + ", empty?" );
    }
  else
    {
      Helper::safe_getline( IN1 , line );
      if ( IN1.eof() || line == "" ) Helper::halt( "problem reading from " + filename + ", empty?" );
    }
  
      
  // has a header row (whether we want to use it or not)

  if ( line[0] == '#' ) 
    {
      has_header_labels = true;
      if ( has_arg_labels ) 
	logger << "  ignoring header row in " << filename << " as channel labels specified with --chs\n" ;
      else
	{
	  line = line.substr(1);
	  labels = Helper::parse( line , "\t ," );
	}
    } 

  // if no arg or header labels, we need to make something up 
  else if ( ! has_arg_labels ) 
    {
      std::vector<std::string> tok = Helper::parse( line , "\t ," );
      labels.resize( tok.size() );
      for (int l=0;l<labels.size();l++) labels[l] = "S" + Helper::int2str(l+1) ;
    }

  // and rewind file to start from the beginning, if needed
  
  if ( ! has_header_labels ) 
    {
      if ( compressed )
	{
	  ZIN1.clear();
	  ZIN1.seekg(0, std::ios::beg);
	}
      else
	{
	  IN1.clear();
	  IN1.seekg(0, std::ios::beg);
	}
    }

  
  const int ns = labels.size();

  //
  // Scan file to get number of records
  //
  
  int np = 0;
  while ( !IN1.eof() ) 
    {
      std::string line;

      if ( compressed )
	{
	  Helper::safe_getline( ZIN1 , line );
	  if ( ZIN1.eof() ) break;
	}
      else
	{
	  Helper::safe_getline( IN1 , line );
	  if ( IN1.eof() ) break;
	}
      
	  
      if ( line == "" ) continue;
      ++np;
    }

  // will ignore any partial records at the end of the file
  int nr = np / Fs;
  np = nr * Fs;

  if ( compressed ) 
    IN1.close();
  else
    IN1.close();

  // re-read
  std::ifstream IN2;
  gzifstream ZIN2;
  
  if ( compressed ) 
    ZIN2.open( filename.c_str() );
  else
    IN2.open( filename.c_str() , std::ios::in );

  // skip header?
  if ( has_header_labels ) 
    {
      std::string dummy;

      if ( compressed )	
	Helper::safe_getline( ZIN2 , dummy );
      else
	Helper::safe_getline( IN2 , dummy );	
    }
  

  //
  // Set header
  //

  header.version = "0";
  header.patient_id = id;
  header.recording_info = "";
  header.startdate = startdate;
  header.starttime = starttime;
  header.nbytes_header = 256 + ns * 256;
  header.ns = 0; // these will be added by add_signal()
  header.ns_all = ns; // check this... should only matter for EDF access, so okay... 
  header.nr = header.nr_all = nr;  // likewise, value of nr_all should not matter, but set anyway
  header.record_duration = 1;
  header.record_duration_tp = header.record_duration * globals::tp_1sec;

  

  //
  // create a timeline
  //

  set_edf();

  set_continuous();

  timeline.init_timeline();

  //
  // read data
  //

  logger << "  reading " << ns << " signals, " 
	 << nr << " seconds ("
	 << np << " samples " << Fs << " Hz) from " << filename << "\n";

  Data::Matrix<double> data( np , ns );
  
  for (int p=0;p<np;p++)
    for (int s=0;s<ns;s++)
      {

	if ( compressed )
	  ZIN2 >> data(p,s);
	else
	  IN2 >> data(p,s);
	
	if ( IN2.eof() ) 
	  Helper::halt( filename + " does not contain enough data-points given parameters\n" );
      }
  
  double dd;

  if ( compressed )
    {
      ZIN2 >> dd;
      if ( ! ZIN2.eof() ) 
	logger << " ** warning, truncating potential trailing sample points (<1 second) from end of input\n";
    }
  else
    {
      IN2 >> dd;
      if ( ! IN2.eof() ) 
	logger << " ** warning, truncating potential trailing sample points (<1 second) from end of input\n";
    }
  
  // should now be end of file...

  if ( compressed )
    ZIN2.close();
  else
    IN2.close();


  //
  // resize data[][], by adding empty records
  //

  for (int r=0;r<nr;r++)
    {
      edf_record_t record( this ); 
      records.insert( std::map<int,edf_record_t>::value_type( r , record ) );
    }

  //
  // add signals (this populates channel-specific 
  //
  
  for (int s=0;s<ns;s++)
    add_signal( labels[s] , Fs , *data.col(s).data_pointer() );

  return true;
}



bool edf_t::attach( const std::string & f , 
		    const std::string & i , 
		    const std::set<std::string> * inp_signals )
{
  
  //
  // Store filename and ID
  //

  
  // expand() expands out any ~/ notation to full path
  filename = Helper::expand( f ) ;

  id = i; 

  //
  // EDF or EDFZ?
  //
  
  file = NULL; 

  edfz = NULL;

  bool edfz_mode = Helper::file_extension( filename , "edfz" );
  
  
  //
  // Attach the file
  //
  
  if ( ! edfz_mode ) 
    {
      if ( ( file = fopen( filename.c_str() , "rb" ) ) == NULL )
	{      
	  file = NULL;
	  logger << " PROBLEM: could not open specified EDF: " << filename << "\n";
	  globals::problem = true;
	  return false;
	}
    }
  else
    {
      edfz = new edfz_t;
      
      // this also looks for the .idx, which sets the record size
      if ( ! edfz->open_for_reading( filename ) ) 
	{
	  delete edfz;
	  edfz = NULL;
	  logger << " PROBLEM: could not open specified .edfz (or .edfz.idx) " << filename << "\n";
	  globals::problem = true;
	  return false;
	}
    }

  
  //
  // Does this look like a valid EDF (i.e. at least contains a header?)
  //

  uint64_t fileSize = 0 ; 

  // for EDF
  if ( file ) 
    {

      fileSize = edf_t::get_filesize( file );
      
      if ( fileSize < 256 ) 
	{
	  logger << " PROBLEM: corrupt EDF, file < header size (256 bytes): " << filename << "\n";
	  globals::problem = true;
	  return false;
	}
    }
  else
    {
      // TODO... need to check EDFZ file. e.g. try reading the last record?
      
    }

  //
  // Read and parse the EDF header (from either EDF or EDFZ)
  //
  
  // Parse the header and extract signal codes 
  // store so we know how to read records
  
  inp_signals_n = header.read( file , edfz , inp_signals );
  
  
  //
  // Swap out any signal label aliases at this point
  //
  
  swap_in_aliases();
  
  //
  // EDF+ requires a time-track
  //
  
  if ( header.edfplus && header.time_track() == -1 ) 
    {
      if ( !header.continuous ) 
	Helper::halt( "EDF+D with no time track" );

      logger << " EDF+ [" << filename << "] did not contain any time-track: adding...\n";

      add_continuous_time_track();

    }

  
  //
  // Record details about byte-size of header/records
  //
  
  header_size = 256 + header.ns_all * 256;
  
  record_size = 0;
  
  for (int s=0;s<header.ns_all;s++)
    record_size += 2 * header.n_samples_all[s] ; // 2 bytes each

  
  if ( edfz ) 
    {
      if ( record_size != edfz->record_size ) 
	Helper::halt( "internal error, different record size in EDFZ header versus index" );      
    }
  

  //
  // Create timeline (relates time-points to records and vice-versa)
  // Here we assume a continuous EDF, but timeline is set up so that 
  // this need not be the case
  //

  timeline.init_timeline();


  //
  // Check remaining file size, based on header information
  //    
  
  if ( file ) 
    {
      uint64_t implied = (uint64_t)header_size + (uint64_t)header.nr_all * record_size;
      
      if ( fileSize != implied ) 
	{

	  std::stringstream msg;
	  msg << "num signals = " << header.ns_all << "\n"
	      << "header size ( = 256 + # signals * 256 ) = " << header_size << "\n"
	      << "record size = " << record_size << "\n"
	      << "numebr of records = " << header.nr_all << "\n"
	      << "implied EDF size from header = " << header_size << " + " << record_size << " * " << header.nr_all << " = " << implied << "\n"
	      << "assuming header correct, means observed has " <<  (double)(fileSize-header_size)/(double)record_size - (double)(implied-header_size)/(double)record_size 
	      << " records too many\n"
	      << "  (where one record is " << header.record_duration << " seconds)\n";
	  
	  Helper::halt( "corrupt EDF: expecting " + Helper::int2str(implied) 
			+ " but observed " + Helper::int2str( fileSize) + " bytes" + "\n" + msg.str() );
	}
    }


  //
  // Output some basic information
  //

  logger << " duration: " << Helper::timestring( timeline.total_duration_tp );

  clocktime_t et( header.starttime );
  if ( et.valid )
    {
      double time_hrs = ( timeline.last_time_point_tp * globals::tp_duration ) / 3600.0 ;
      et.advance( time_hrs );
      logger << " ( clocktime " << header.starttime << " - " << et.as_string() << " )";
    }
  logger << "\n";
  
  //	 << " hms, last time-point " << Helper::timestring( ++timeline.last_time_point_tp ) << " hms after start\n";

  if ( globals::verbose )
    logger << "  " << header.nr_all  << " records, each of " << header.record_duration << " second(s)\n";
  
  logger << "\n signals: " << header.ns << " (of " << header.ns_all << ") selected ";
  logger << "in " << ( header.edfplus ? "an EDF+" : "a standard EDF" ) << " file:" ;
  for (int s=0;s<header.ns;s++) 
    logger << ( s % 8 == 0 ? "\n  " : " | " ) << header.label[s]; 
  logger << "\n";

  return true;

}



void edf_t::swap_in_aliases()
{

  // simply get a wildcard-ed signal_list_t
  // as this process of searching for all signals also 
  // swaps in the alias and updates the EDF header
  
  signal_list_t dummy = header.signal_list( "*" );
  
}


std::vector<double> edf_t::fixedrate_signal( uint64_t start , 
					     uint64_t stop , 
					     const int signal , 
					     const int downsample ,
					     std::vector<uint64_t> * tp , 
					     std::vector<int> * rec ) 
{


  std::vector<double> ret;

  if ( tp != NULL ) 
    tp->clear();

  if ( rec != NULL ) 
    rec->clear();

  //
  // Ensure we are within bounds
  //
  
  if ( stop > timeline.last_time_point_tp + 1 )
    stop = timeline.last_time_point_tp + 1 ;      
  
  //
  // First, determine which records are being requested?
  //
  
  const uint64_t n_samples_per_record = header.n_samples[signal];
  
   //   std::cerr << "signal = " << signal << "\t" << header.n_samples.size() << "\t" << header.n_samples_all.size() << "\n";
   // std::cerr << "SR " << n_samples_per_record << "\n";
 
  int start_record, stop_record;
  int start_sample, stop_sample;

  //    std::cerr << "looking for " << start << " to " << stop << "\n";

  bool okay = timeline.interval2records( interval_t( start , stop ) , 
					 n_samples_per_record , 
					 &start_record, &start_sample , 
					 &stop_record, &stop_sample );
  
  
   // std::cerr << "records start = " << start_record << " .. " << start_sample << "\n";
   // std::cerr << "records stop  = " << stop_record << " .. " << stop_sample << "\n";

  //
  // If the interval is too small (or is applied to a signal with a low sampling rate)
  // we might not find any sample-points in this region.   Not an error per se, but flag
  // (we have to check that all downstream functons will play nicely with an empty set being returned)
  //
  
  if ( ! okay ) 
    {
      logger << " ** warning ... empty intervals returned (check intervals/sampling rates)\n";
      return ret; // i.e. empty
    }

  
  //
  // Ensure that these records are loaded into memory
  // (if they are already, they will not be re-read)
  //
  
  bool retval = read_records( start_record , stop_record );
  
  //  std::cerr << "read records: " << retval << "\n";
  
  //
  // Copy data into a single vector
  //
 
  double bitvalue = header.bitvalue[ signal ];
  double offset   = header.offset[ signal ];

  int r = start_record;

  while ( r <= stop_record )
    {
      //std::cerr << "rec " << r << "\n";

      // std::cout << records.size() << " is REC SIZE\n";
      // std::cout << "foudn " << ( records.find( r ) != records.end() ? " FOUND " : "NOWHERE" ) << "\n";

      const edf_record_t * record = &(records.find( r )->second);

      //std::cerr << " test for NULL " << ( record == NULL ? "NULL" : "OK" ) << "\n";
      
      const int start = r == start_record ? start_sample : 0 ;
      const int stop  = r == stop_record  ? stop_sample  : n_samples_per_record - 1;

      // std::cerr << " start, stop = " << start << "   " << stop << "\n";
      
      // std::cerr << "OUT\t"
      // 		<< record->data.size() << " "
      // 		<< signal << " " 
      // 		<< header.ns << "\n";
      
      for (int s=start;s<=stop;s+=downsample)
	{
	  // convert from digital to physical on-the-fly
	  ret.push_back( edf_record_t::dig2phys( record->data[ signal ][ s ] , bitvalue , offset ) );
	  
	  if ( tp != NULL ) 
	    tp->push_back( timeline.timepoint( r , s , n_samples_per_record ) );
	  if ( rec != NULL ) 
	    rec->push_back( r );
	}
      
      r = timeline.next_record(r);
      if ( r == -1 ) break;
    }

  return ret;  
}



//
// Functions to write an EDF
//


bool edf_header_t::write( FILE * file )
{
  
  // regarding the nbytes_header variable, although we don't really
  // use it, still ensure that it is properly set (i.e. we may have
  // added/removed signals, so we need to update before making the EDF
  
  nbytes_header = 256 + ns * 256;
  
  writestring( version , 8 , file );
  writestring( patient_id , 80 , file );
  writestring( recording_info , 80 , file );
  writestring( startdate , 8 , file );
  writestring( starttime , 8 , file );
  writestring( nbytes_header , 8 , file );
  fwrite( reserved.data() , 1 , 44 , file );
  writestring( nr , 8 , file );
  writestring( record_duration , 8 , file );
  writestring( ns , 4 , file );

  // for each of 'ns' signals
  
  for (int s=0;s<ns;s++)
    writestring( label[s], 16, file );
  
  for (int s=0;s<ns;s++)
    writestring( transducer_type[s], 80, file );

  for (int s=0;s<ns;s++)
    writestring( phys_dimension[s], 8, file );

  for (int s=0;s<ns;s++)
    writestring( physical_min[s], 8, file );

  for (int s=0;s<ns;s++)
    writestring( physical_max[s], 8, file );

  for (int s=0;s<ns;s++)
    writestring( digital_min[s], 8, file );

  for (int s=0;s<ns;s++)
    writestring( digital_max[s], 8, file );

  for (int s=0;s<ns;s++)
    writestring( prefiltering[s], 80, file );

  for (int s=0;s<ns;s++)
    writestring( n_samples[s], 8, file );
  
  for (int s=0;s<ns;s++)
    writestring( signal_reserved[s], 32, file );
  
  return true;
}



bool edf_header_t::write( edfz_t * edfz )
{
  
  // regarding the nbytes_header variable, although we don't really
  // use it, still ensure that it is properly set (i.e. we may have
  // added/removed signals, so we need to update before making the EDF
  nbytes_header = 256 + ns * 256;
  
  edfz->writestring( version , 8 );
  edfz->writestring( patient_id , 80 );
  edfz->writestring( recording_info , 80 );
  edfz->writestring( startdate , 8 );
  edfz->writestring( starttime , 8 );
  edfz->writestring( nbytes_header , 8 );
  edfz->write( (byte_t*)reserved.data() , 44 );
  edfz->writestring( nr , 8 );
  edfz->writestring( record_duration , 8 );
  edfz->writestring( ns , 4 );

  // for each of 'ns' signals
  
  for (int s=0;s<ns;s++)
    edfz->writestring( label[s], 16 );
  
  for (int s=0;s<ns;s++)
    edfz->writestring( transducer_type[s], 80 );

  for (int s=0;s<ns;s++)
    edfz->writestring( phys_dimension[s], 8 );

  for (int s=0;s<ns;s++)
    edfz->writestring( physical_min[s], 8 );

  for (int s=0;s<ns;s++)
    edfz->writestring( physical_max[s], 8 );

  for (int s=0;s<ns;s++)
    edfz->writestring( digital_min[s], 8  );

  for (int s=0;s<ns;s++)
    edfz->writestring( digital_max[s], 8 );

  for (int s=0;s<ns;s++)
    edfz->writestring( prefiltering[s], 80 );

  for (int s=0;s<ns;s++)
    edfz->writestring( n_samples[s], 8 );
  
  for (int s=0;s<ns;s++)
    edfz->writestring( signal_reserved[s], 32 );
  
  return true;
}





bool edf_record_t::write( FILE * file )
{

  
  for (int s=0;s<edf->header.ns;s++)
    {
      
      const int nsamples = edf->header.n_samples[s];

      //
      // Normal data channel
      //

      if ( edf->header.is_data_channel(s) )
	{      
	  for (int j=0;j<nsamples;j++)
	    {	  
	      char a , b;
	      dec2tc( data[s][j] , &a, &b );	  
	      fputc( a , file );
	      fputc( b , file );
	    }
	}
      
      //
      // EDF Annotations channel
      //
      
      if ( edf->header.is_annotation_channel(s) )
	{      	  	  
	  for (int j=0;j< 2*nsamples;j++)
	    {	  	      
	      char a = j >= data[s].size() ? '\x00' : data[s][j];	      
	      fputc( a , file );	      
	    }
	}
    
    }

  return true;
}


bool edf_record_t::write( edfz_t * edfz )
{

  // check if this has been read?
  
  for (int s=0;s<edf->header.ns;s++)
    {
      
      const int nsamples = edf->header.n_samples[s];

      //
      // Normal data channel
      //

      if ( edf->header.is_data_channel(s) )
	{  
	  std::vector<char> d( 2 * nsamples );
	  
	  for (int j=0;j<nsamples;j++)
	    dec2tc( data[s][j] , &(d)[2*j], &(d)[2*j+1] );	  

	  edfz->write( (byte_t*)&(d)[0] , 2 * nsamples );
	  
	}
      
      //
      // EDF Annotations channel
      //
      
      if ( edf->header.is_annotation_channel(s) )
	{      	  	  

	  std::vector<char> d( 2 * nsamples );

	  for (int j=0;j< 2*nsamples;j++)
	    {	  	      
	      char a = j >= data[s].size() ? '\x00' : data[s][j];	      
	      d[j] = a;
	    }
	  
	  edfz->write( (byte_t*)&(d)[0] , 2 * nsamples ); 
	  
	}
    
    }

  return true;
}



bool edf_t::write( const std::string & f , bool as_edfz )
{

  reset_start_time();

  filename = f;

  if ( ! as_edfz ) 
    {

      FILE * outfile = NULL;
      
      if ( ( outfile = fopen( filename.c_str() , "wb" ) ) == NULL )      
	{
	  logger << " ** could not open " << filename << " for writing **\n";
	  return false;
	}
      
      header.write( outfile );
      
      int r = timeline.first_record();
      while ( r != -1 ) 
	{
	  
	  // we may need to load this record, before we can write it
	  if ( ! loaded( r ) )
	    {
	      edf_record_t record( this ); 
	      record.read( r );
	      records.insert( std::map<int,edf_record_t>::value_type( r , record ) );	      
	    }
	  
	  records.find(r)->second.write( outfile );
	  r = timeline.next_record(r);
	}
      
      fclose(outfile);
    }

  //
  // .edfz and .edfz.idx
  //

  else 
    {

      edfz_t edfz;

      if ( ! edfz.open_for_writing( filename ) )
	{
	  logger << " ** could not open " << filename << " for writing **\n";
	  return false;
	}

      // write header (as EDFZ)
      header.write( &edfz );
      
      int r = timeline.first_record();
      while ( r != -1 ) 
	{
	  
	  // we may need to load this record, before we can write it
	  if ( ! loaded( r ) )
	    {
	      edf_record_t record( this ); 
	      record.read( r );
	      records.insert( std::map<int,edf_record_t>::value_type( r , record ) );	      
	    }
	  
	
	  // set index	  
	  int64_t offset = edfz.tell();	  
	  edfz.add_index( r , offset );
	  
	  // now write to the .edfz
	  records.find(r)->second.write( &edfz );
	  
	  // next record
	  r = timeline.next_record(r);
	}
      

      //
      // Write .idx
      //
      
      logger << "  writing EDFZ index to " << filename << ".idx\n";

      edfz.write_index( record_size );

      
      //
      // All done
      //

      edfz.close();


    }
  

  return true;
}




void edf_t::drop_signal( const int s )
{

  if ( s < 0 || s >= header.ns ) return;  
  --header.ns;

  // need to track whether this signal was in the list of signals to be read from the original file
  //  -- it needn't be, i.e. if a new channel has been created
  //  -- but if it is, we need to know this when we subsequently read in
  //     new data from disk

  // i.e. it is not whether it was on disk per se, it is whether it would have been included
  //      via an initial sig= specification, i.e. inp_signals for edf_t::attach()
  //      so then we can remove it from edf.inp_signals_n[]
  
  // old; does not respect alias use
  //bool present_in_EDF_file = header.label_all.find( header.label[s] ) != header.label_all.end() ;
  //int os = present_in_EDF_file = ? header.label_all[ header.label[ s ] ] : -1 ;

  // get original signal slot number (-1 if not present)
  int os = header.original_signal( header.label[ s ] ) ;

  // alter header
  header.label.erase( header.label.begin() + s );
  header.annotation_channel.erase( header.annotation_channel.begin() + s );
  header.transducer_type.erase( header.transducer_type.begin() + s );
  header.phys_dimension.erase( header.phys_dimension.begin() + s );
  header.physical_min.erase( header.physical_min.begin() + s );
  header.physical_max.erase( header.physical_max.begin() + s );
  header.digital_min.erase( header.digital_min.begin() + s );
  header.digital_max.erase( header.digital_max.begin() + s );
  header.orig_physical_min.erase( header.orig_physical_min.begin() + s );
  header.orig_physical_max.erase( header.orig_physical_max.begin() + s );
  header.orig_digital_min.erase( header.orig_digital_min.begin() + s );
  header.orig_digital_max.erase( header.orig_digital_max.begin() + s );
  header.prefiltering.erase( header.prefiltering.begin() + s );
  header.n_samples.erase( header.n_samples.begin() + s );
  header.signal_reserved.erase( header.signal_reserved.begin() + s );
  header.bitvalue.erase( header.bitvalue.begin() + s );
  header.offset.erase( header.offset.begin() + s );
  
  // remove from 'primary input' list (i.e. which is used
  // when reading a new record;  these signal numbers
  // are in the original (EDF-based) counting scheme
  
  if ( os != -1 ) // i.e. present in original signal list
    {
//       std::cout << "inp sz = " << inp_signals_n.size() << "\n";
//       std::cout << "in " << (inp_signals_n.find(os) != inp_signals_n.end() ) << "\n";
      inp_signals_n.erase( inp_signals_n.find(os) );
    }
  
  // need to remake label2header
  header.label2header.clear();
  for (int l=0;l<header.label.size();l++)     
    if ( header.is_data_channel(l) ) 
      header.label2header[ header.label[l] ] = l;      
  
  // records
  int r = timeline.first_record();
  while ( r != -1 )
    {
      if ( records.find(r) != records.end() ) 
	records.find(r)->second.drop(s);
      r = timeline.next_record(r);
    }
  
  
}

void edf_record_t::drop( const int s )
{
  data[ s ].clear();
  data.erase( data.begin() + s );
//   pdata[ s ].clear();
//   pdata.erase( pdata.begin() + s );
}

void edf_t::add_signal( const std::string & label , const int Fs , const std::vector<double> & data )
{
  const int ndata = data.size();

  const int n_samples = Fs * header.record_duration ;
  
  if ( ndata == 0 ) 
    {
      logger << " **empty EDF, not going to add channel " << label << " **\n";
      return;
    }

  //  std::cout << "nd = " << ndata << " " << header.nr << " " << n_samples << "\n";

  // sanity check -- ie. require that the data is an appropriate length
  if ( ndata != header.nr * n_samples ) 
    Helper::halt( "internal error: problem with length of input data" );  

  // get physical signal min/max to determine scaling
  
  double pmin = data[0];
  double pmax = data[0];
  
  for (int i=1;i<ndata;i++) 
    {
      if      ( data[i] < pmin ) pmin = data[i];
      else if ( data[i] > pmax ) pmax = data[i];
    }

  // determine bitvalue and offset

  //header
  const int16_t dmax = 32767;
  const int16_t dmin = -32768;

  double bv = ( pmax - pmin ) / (double)( dmax - dmin );
  double os = ( pmax / bv ) - dmax;

  // store (after converting to digital form)
  
  int c = 0;
  int r = timeline.first_record();

  while ( r != -1 ) 
    {

      std::vector<int16_t> t(n_samples);
      
      for (int i=0;i<n_samples;i++) 
	t[i] = edf_record_t::phys2dig( data[c++] , bv , os );
      
      records.find(r)->second.add_data(t);
      
      r = timeline.next_record(r);
    }
    
  // add to header
  ++header.ns;
    

  header.bitvalue.push_back( bv );
  header.offset.push_back( os );
  
  header.label.push_back( label );
  
  if ( ! Helper::imatch( label , "EDF Annotation" , 14 ) )
    header.label2header[label] = header.label.size()-1;     

  header.annotation_channel.push_back( ( header.edfplus ? 
					 Helper::imatch( label , "EDF Annotation" , 14 ) :
					 false ) ) ;

  header.transducer_type.push_back( "n/a" );
  header.phys_dimension.push_back( "n/a" );
  header.physical_min.push_back( pmin );
  header.physical_max.push_back( pmax );
  header.digital_min.push_back( dmin );
  header.digital_max.push_back( dmax );
  header.orig_physical_min.push_back( pmin );
  header.orig_physical_max.push_back( pmax );
  header.orig_digital_min.push_back( dmin );
  header.orig_digital_max.push_back( dmax );
  header.prefiltering.push_back( "n/a" );
  header.n_samples.push_back( n_samples );  
  header.signal_reserved.push_back( "" );  
  
}

std::vector<double> edf_record_t::get_pdata( const int s )
{
  const double & bv     = edf->header.bitvalue[s];
  const double & offset = edf->header.offset[s];
  const int n = data[s].size();
  std::vector<double> r( n );
  for ( int i = 0 ; i < n ; i++ ) r[i] = dig2phys( data[s][i] , bv , offset );
  return r;
}

void edf_record_t::add_data( const std::vector<int16_t> & d )
{
  // store
  data.push_back( d );
}

void edf_record_t::add_annot( const std::string & str )
{
  // create a new data slot
  std::vector<int16_t> dummy; data.push_back(dummy);
  //std::vector<double> pdummy;   pdata.push_back(pdummy);
  // add this to the end
  add_annot( str , data.size()-1 );
}

void edf_record_t::add_annot( const std::string & str , const int signal )
{
  
  if ( signal < 0 || signal >= data.size() ) 
    Helper::halt( "internal error in add_annot()" );
  
  // convert text to int16_t encoding
  data[ signal ].resize( str.size() );
  for (int s=0;s<str.size();s++) 
    {
      data[signal][s] = (char)str[s];
    } 
}

// now redundant
// void edf_record_t::calc_data( double bitvalue , double offset  )
// {
  
//   // convert to physical scale
//   // pdata[s][j] = header->bitvalue[s] * ( header->offset[s] + d );
  
//   const std::vector<double> & pd = pdata[ pdata.size() - 1 ];
//   const int n = pd.size();
//   std::vector<int> d( n );
//   for (int i=0;i<n;i++) d[i] = pd[i]/bitvalue - offset;      
  
//   // create data given min/max etc
//   data.push_back( d );
// }


void edf_t::reset_record_size( const double new_record_duration )
{

  if ( ! header.continuous )
    Helper::halt( "can only change record size for EDF, not EDF+, currently" );

  // this changes the in-memory representation;
  // naturally, new data cannot easily be loaded from disk, so 
  // this command should always write a new EDF and then quit
  
  // original record size (seconds) , and derived value in tp
  // double record_duration;
  // uint64_t record_duration_tp;

  // required : new_record_duration

  // nothing to do?
  if ( header.record_duration == new_record_duration ) return;
  
  std::vector<int> new_nsamples;

  int new_record_size = 0;

  // check that all signals can fit evenly into the new record size
  for (int s=0;s<header.ns;s++)
    {

      if ( header.is_annotation_channel(s) )
	Helper::halt( "cannot change record size for EDF annotations: drop this signal first" );

      int    nsamples = header.n_samples[s];
      double fs = (double)nsamples / header.record_duration;
      
      // does new record size contain an integer number of sample points?

      double implied = new_record_duration * fs;
      
      int   new_nsamples1 = implied;

      if ( fabs( (double)new_nsamples1 - implied ) > 0 ) 
	Helper::halt( "bad value of ns" );
      
      new_nsamples.push_back( new_nsamples1 );

      // track for record size of the new EDF 
      new_record_size += 2 * new_nsamples1 ; 
      
    }
  
  // buffer for new records
  edf_record_t new_record( this );

  std::map<int,edf_record_t> new_records;
  
  // manually change size of the new buffer record
  for (int s = 0 ; s < header.ns ; s++)
    new_record.data[s].resize( new_nsamples[s] , 0 );
    

  // get implied number of new records (truncate if this goes over)
  int new_nr = floor( header.nr * header.record_duration ) / (double) new_record_duration ;

  for (int r=0;r<new_nr;r++) 
    new_records.insert( std::map<int,edf_record_t>::value_type( r , new_record ) );
  
  // process one signal at a time
  std::vector<int> new_rec_cnt( header.ns , 0 );
  std::vector<int> new_smp_cnt( header.ns , 0 );
  
  int r = timeline.first_record();
  while ( r != -1 ) 
    {
  
      ensure_loaded( r );

      edf_record_t & record = records.find(r)->second;

      for (int s = 0 ; s < header.ns ; s++ )
	{

	  const int n = header.n_samples[s];

	  for (int i = 0 ; i < n ; i++ )
	    {
	      
	      if ( new_smp_cnt[s] == new_nsamples[s] )
		{
		  ++new_rec_cnt[s];
		  new_smp_cnt[s] = 0;
		}
	      
	      if ( new_rec_cnt[s] < new_nr )
		{
		  std::map<int,edf_record_t>::iterator rr = new_records.find( new_rec_cnt[s] );
		  if ( rr == new_records.end() ) Helper::halt( "internal error" );
		  edf_record_t & new_record = rr->second;

//  		  std::cout << "setting " << new_rec_cnt[s] << "\t" << new_smp_cnt[s] << " = " << r << " " << i << "\n";
//  		  std::cout << " sz = " << new_record.data[ s ].size() << " " << record.data[ s ].size() << "\n";
		  new_record.data[ s ][ new_smp_cnt[ s ] ] = record.data[ s ][ i ];
		  
		  ++new_smp_cnt[ s ];
		}

	    } // next sample point
	  
	} // next signal

      r = timeline.next_record(r);

    } // next record
  

  //
  // copy over
  //
  
  records = new_records;
  new_records.clear();

  //
  // and update EDF header
  //

  header.nr = new_nr;
  header.n_samples = new_nsamples;
  header.record_duration = new_record_duration;
  header.record_duration_tp = header.record_duration * globals::tp_1sec;

  // also, update edf.record_size : we won't be reading anything else from the original 
  // EDF, but if we are writing an EDFZ, then edf_t needs the /new/ record size for the
  // index

  record_size = new_record_size ; 

  // make a new timeline 
  timeline.re_init_timeline();

  // all done

}


void edf_t::reference_and_scale( const int s , const int r , const double rescale )
{
  
  //
  // reference and/or rescale 
  //
  
  if ( s < 0 || s >= header.ns ) Helper::halt( "incorrectly specified signal" );
  
  bool hasref = r != -1;
  if ( r < -1 || r >= header.ns || r == s ) Helper::halt( "incorrectly specified reference" );
  
  //
  // check comparable sampling rate
  //
  
  if ( hasref && header.n_samples[ s ] != header.n_samples[ r ] ) 
    Helper::halt( "reference must have similar sampling rate" );
  
  const int ns = header.n_samples[ s ];
  
  
  //
  // for every record (masked or otherwise), 
  // subtract out the reference and rescale (e.g. mV -> uV)
  // 
  
  std::vector<double> d; 
  
  int rec = timeline.first_record();
  while ( rec != -1 )
    {

      ensure_loaded( rec );
      
      edf_record_t & record = records.find(rec)->second;
      
      if ( hasref )
	{
	  std::vector<double> pdata_sig = record.get_pdata(s);
	  std::vector<double> pdata_ref = record.get_pdata(r);
	  
	  for (int i=0;i<ns;i++) d.push_back( ( pdata_sig[i] - pdata_ref[i] ) * rescale );

	}
      else	
	{
	  std::vector<double> pdata_sig = record.get_pdata(s);
	  for (int i=0;i<ns;i++) 
	    {
	      d.push_back( pdata_sig[i] * rescale );
	      //std::cout << "rescale " << pdata_sig[i] << " " << pdata_sig[i] * rescale << "\n";
	    }
	}
      
      rec = timeline.next_record(rec);
      
    }
  
  // update signal
  update_signal( s , &d );
  
}


void edf_t::reference( const signal_list_t & signals0 ,
		       const signal_list_t & refs ,
		       bool make_new ,
		       const std::string & new_channel , 
		       const int new_sr ,
		       bool dereference )
{

  // copy as we may modify this
  signal_list_t signals = signals0;
  
  const int ns = signals.size();
  const int nr = refs.size();

  // need at least one channel specified

  if ( ns == 0 ) 
    Helper::halt( "must specify sig={ ... }" );


  
  //
  // Create a new channel?
  //

  if ( make_new && ns > 1 )
    Helper::halt( "can only re-reference a single channel if 'new' is specified" );

  if ( make_new )
    {
      // make copy
      copy_signal( header.label[ signals(0) ] , new_channel );

      // switch to re-reference this copy now
      signals = header.signal_list( new_channel );

      // do we need to resample? 
      
      const int sig_sr = (int)header.sampling_freq( signals(0) );
      
      // resample sig, if needed (only one)
      // this slot is the 'new' one, so original signal untouched
      if ( new_sr != 0 && sig_sr != new_sr )
	dsptools::resample_channel( *this , signals(0) , new_sr );
      
      // if the reference needs resampling, we need to copy a new
      // channel and do the re-sampling (i.e. to leave the original
      // untouched).   Do this downstream on the 'final' reference
      // (as this may involve multiple channels that have different 
      // SRs.
      
    }

  
  //
  // if nr size is 0, means leave as is
  // if we've requested a new channel, we need to make this still
  //
  
  if ( nr == 0 ) return;

  //
  // Console logging 
  //

  if ( nr > 0 )
    {
      logger << ( dereference ? " dereferencing" : " referencing" );
      for (int s=0;s<ns;s++) logger << " " << header.label[ signals(s) ];
      logger << " with respect to";
      if ( nr > 1 ) logger << " the average of";
      for (int r=0;r<nr;r++) logger << " " << header.label[ refs(r) ];
      logger << "\n";
    }
  


  //
  // check SR for all channels  
  //
  
  int np_sig = header.n_samples[ signals(0) ];

  if ( (!make_new ) || ( make_new && new_sr == 0 ) ) 
    {
      
      
      for (int s=0;s<ns;s++) 
	if ( header.n_samples[ signals(s) ] != np_sig ) 
	  Helper::halt( "all signals/references must have similar sampling rates" );
      
      for (int r=0;r<nr;r++) 		
	if ( header.n_samples[ refs(r) ] != np_sig ) 
	  Helper::halt( "all signals/references must have similar sampling rates" );
      
    }
  else
    {
      // here we are fixing SR, and we've already done this for the 
      // signal;  we'll do it later for REF, but need to check they
      // all (if >1 ref, i.e. average ref) match

      int np_ref = header.n_samples[ refs(0) ];
      
      for (int r=0;r<nr;r++) 		
	if ( header.n_samples[ refs(r) ] != np_ref ) 
	  Helper::halt( "all references must have similar sampling rates" );

    }


  //
  // Build reference once
  //
  
  std::vector<double> reference;

  // number of samples points per record for reference
  const int np_ref = header.n_samples[ refs(0) ];

  int rec = timeline.first_record();
  while ( rec != -1 )
    {
      ensure_loaded( rec );

      edf_record_t & record = records.find(rec)->second;
      
      std::vector<std::vector<double> > refdata;

      // get data
      for (int r=0;r<nr;r++) 		
	refdata.push_back( record.get_pdata( refs(r) ) );
      
      // average
      for (int i=0;i<np_ref;i++) 
	{
	  double avg = 0;
	  for (int r=0;r<nr;r++) avg += refdata[r][i];
	  if ( nr != 1 ) avg /= (double)nr;
	  reference.push_back( avg );
	}      

      // next record
      rec = timeline.next_record(rec);       
    }


  //
  // We to resample reference?
  //

  if ( make_new && new_sr != 0 )
    {
      const int ref_sr = (int)header.sampling_freq( refs(0) );
      if ( ref_sr != new_sr )
	{
	  const int refsize = reference.size();

	  reference = dsptools::resample( &reference , ref_sr , new_sr );

	  // ensure exact length... pad if needed
	  if ( reference.size() != refsize )
	    reference.resize( refsize );
	}
    }
  
  //
  // transform signals one at a time, now we have reference in 'reference'
  //
  
  for (int s=0;s<signals.size();s++) 
    {
      
      // do not reference to self
      if ( nr == 1 && signals(s) == refs(0) ) 
	{
	  logger << " skipping " << refs.label(0) << " to not re-reference to self\n"; 
	  continue;
	}
      
      // transformed signal      
      std::vector<double> d;
      int cc = 0;

      //
      // iterate over records
      // 
      
      int rec = timeline.first_record();
      while ( rec != -1 )
	{
	  
	  ensure_loaded( rec );
	  
	  // now we can access
	  edf_record_t & record = records.find(rec)->second;
	  
	  std::vector<double> d0 = record.get_pdata( signals(s) );
	  
	  if ( dereference ) 
	    for (int i=0;i<np_sig;i++) d.push_back( d0[i] + reference[cc++] );
	  else	    
	    for (int i=0;i<np_sig;i++) d.push_back( d0[i] - reference[cc++] );
	  
	  // next record
	  rec = timeline.next_record(rec); 
	  
	}
      
      // update signal
      update_signal( signals(s) , &d );
      
      // next signal to re-reference
    }

}


bool edf_t::load_annotations( const std::string & f0 )
{

  //
  // parse annotation filename
  //

  const std::string f = Helper::expand( f0 );


  // allow wildcards
    
  if ( ! Helper::fileExists( f ) ) 
    Helper::halt( "annotation file " + f + " does not exist for EDF " + filename );

  //
  // store filename (if needed to be output in a WRITE to the sample-list)
  //
  
  annot_files.push_back( f );

  //
  // Type of input?
  //

  bool xml_mode = Helper::file_extension( f , "xml" );
  
  bool feature_list_mode = Helper::file_extension( f , "ftr" );
  
  //
  // XML files (NSRR, Profusion or Luna formats)
  //
  
  if ( xml_mode ) 
    {
      annot_t::loadxml( f , this );
      return true;
    }
  

  //
  // Feature lists
  //
  
  if ( feature_list_mode && globals::read_ftr )
    {
      
      std::vector<std::string> tok = Helper::parse( f , "/" );

      std::string file_name = tok[ tok.size()-1];	
    
      // filename should be id_<ID>_feature_<FEATURE>.ftr
      int pos = file_name.find( "_feature_" );
      
      if ( pos == std::string::npos || file_name.substr(0,3) != "id_" )  
	Helper::halt( "bad format for feature list file name: id_<ID>_feature_<FEATURE>.ftr" );
      
      std::string id_name = file_name.substr(3,pos-3);

      if ( id_name != id )
	{
	  Helper::warn( ".ftr file id_{ID} does not match EDF ID : [" + id_name + "] vs [" + id + "]" );
	  return false;
	}
      
      std::string feature_name = file_name.substr( pos+9 , file_name.size() - 4 - pos - 9 );
  
      // are we checking whether to add this file or no? 
      
      if ( globals::specified_annots.size() > 0 && 
	   globals::specified_annots.find( feature_name ) == globals::specified_annots.end() ) return false;
      
      // create and load annotation
      
      annot_t * a = timeline.annotations.add( feature_name );
      
      a->name = feature_name;
      a->description = "feature-list";
      a->file = file_name;

      // load features, and track how many
      aoccur[ feature_name ] = a->load_features( f  );
      
      return true;
    }


  //
  // Otherwise, process as an .annot or .eannot file
  //
  
  return annot_t::load( f , *this );
  
}


int  edf_header_t::signal( const std::string & s , bool silent )
{  
  signal_list_t slist = signal_list(s);
  if ( slist.size() != 1 ) 
    {
      if ( ! silent ) 
	logger << " ** could not find signal [" << s << "] of " << label2header.size() << " signals **\n";
      return -1;
    }  
  return slist(0);
}


bool  edf_header_t::has_signal( const std::string & s )
{
  std::vector<std::string> tok = Helper::parse( s , "|" );    
  for (int t=0;t<tok.size();t++)
    {
      // primary name (that might be an alias)?
      if ( label2header.find(tok[t]) != label2header.end() )
	return true;
      
      // using aliased (i.e. original) name?
      if ( cmd_t::label_aliases.find( tok[t] ) != cmd_t::label_aliases.end() )
	return true;
    }
  return false;
}


int  edf_header_t::original_signal_no_aliasing( const std::string & s  )
{  
  std::map<std::string,int>::const_iterator ff = label_all.find( s );
  if ( ff != label_all.end() ) return ff->second;
  return -1;
}


int  edf_header_t::original_signal( const std::string & s  )
{  

  // look up, with aliases, in original
  // label_all[ ]
  
  std::map<std::string,int>::const_iterator ff = label_all.find( s );
  
  if ( ff != label_all.end() ) return ff->second;
  
  // otherwise, consider if we have aliases
  if ( cmd_t::label_aliases.find( s ) != cmd_t::label_aliases.end() )
    {
      const std::string & s2 = cmd_t::label_aliases[ s ];
      ff = label_all.find( s2 );
      if ( ff != label_all.end() ) return ff->second;
    }

  if ( cmd_t::primary_alias.find( s ) != cmd_t::primary_alias.end() )
    {
      std::vector<std::string> & a = cmd_t::primary_alias.find( s )->second;
      for (int i=0;i<a.size();i++)
	{
	  ff = label_all.find( a[i] );
	  if ( ff != label_all.end() ) return ff->second;
	}
    }
  
  return -1;
  
}


signal_list_t edf_header_t::signal_list( const std::string & s , bool no_annotation_channels , bool show_warnings )
{
  
  signal_list_t r;
  
  // wildcard means all signals '*'

  if ( s == "*" )
    {
      for (int s=0;s<label.size();s++)
	{
	  
	  // ? only consider data tracks
	  
	  if ( no_annotation_channels  && 
	       is_annotation_channel( s ) ) continue;
	  
	  std::string lb = label[s];
	  	  
	  // swap in alias?
	  if ( cmd_t::label_aliases.find( lb ) != cmd_t::label_aliases.end() ) 
	    {
	      // track
	      aliasing[ cmd_t::label_aliases[ lb ] ] = lb;

	      //swap
	      lb = cmd_t::label_aliases[ lb ];
	      label2header[ lb ] = s;
	      label[s] = lb;

	      
	    }
	  
	  r.add( s, lb );
	}
    }
  
  // comma-delimited; but within a signal, can have options
  // that are pipe-delimited  
  
  std::vector<std::string> tok = Helper::quoted_parse( s , "," );    
  for (int t=0;t<tok.size();t++)    
    {

      std::vector<std::string> tok2_ = Helper::quoted_parse( tok[t] , "|" );    

      // first swap in any aliases, and place those at the front of the list
      // then continue as before

      // swap in alias first? -- this may double alias, but fine.
      
      // eig. 
      // alias   sigX|sigY|sigZ
      // signal  sigY|sigZ|sig0
      // will make all --> sigX (which is correct)

      std::string alias = "";
      for (int t2=0;t2<tok2_.size();t2++)    
	{
	  if ( cmd_t::primary_alias.find( tok2_[t2] ) != cmd_t::primary_alias.end() )
	    {
	      //	      std::cout << "tok2_ " << tok2_[t2] << "and alias ["<<alias<< "]\n";
	      if ( alias == "" ) alias = tok2_[t2];
	      else if ( alias != tok2_[t2] )
		Helper::halt( "more than one alias implied" );
	      //std::cout << "tok2_ " << tok2_[t2] << "and alias ["<<alias<< "]\n";
	    }
	  else if ( cmd_t::label_aliases.find( tok2_[t2] ) != cmd_t::label_aliases.end() ) 
	    {
	      if ( alias == "" ) 
		alias = cmd_t::label_aliases[ tok2_[t2] ];	  
	      else if ( alias != cmd_t::label_aliases[ tok2_[t2] ] )
		Helper::halt( "more than one alias implied" );
	    }
	}
      
      // update list if needed
      std::vector<std::string> tok2;
      if ( alias != "" ) 
	{
	  tok2.push_back( alias );
	  const std::vector<std::string> & avec = cmd_t::primary_alias.find( alias )->second;
	  std::vector<std::string>::const_iterator aa = avec.begin();
	  while ( aa != avec.end() )
	    {
	      //std::cout << "adding " << *aa << "\n";
	      tok2.push_back( *aa );
	      ++aa;
	    }	  
	  for (int t2=0;t2<tok2_.size();t2++) 
	    {
	      if ( tok2_[t2] != alias ) 
		tok2.push_back( tok2_[t2] );   
	    }
	}
      else
	tok2 = tok2_;
      
      std::set<int> added;
      
      // proceed as before
      for (int t2=0;t2<tok2.size();t2++)    
	{
// 	  std::cout << "t2 = " << t2 << "\t" << tok2[t2] << "\n";
// 	  std::cout << "label2header.find size " << label2header.size() << "\n";

	  // add first match found 
	  if ( label2header.find(tok2[t2]) != label2header.end() ) 
	    {

	      const int l = label2header[tok2[t2]];
	      
	      //std::cout << "found match " << l << "\n";

	      if ( t2 > 0 ) // relabel if wasn't first choice?
		{
		  label2header[ tok2[0] ] = l;
		  label[l] = tok2[0];
		}	  
	      
	      //std::cout << "adding N " << label2header[tok2[0]]  << "\n";
	      if ( added.find( label2header[tok2[0]] ) == added.end() )
		{
		  r.add( label2header[tok2[0]] , tok2[0] ); 
		  added.insert( label2header[tok2[0]] ) ;
		}		  

	      break;
	    }
	}
      
    }

  return r;
}

void edf_header_t::rename_channel( const std::string & old_label , const std::string & new_label )
{
  // expects exact match (i.e. this only called from XML <Signals> / <CanonicalLabel> information  
  for (int s=0;s<label.size();s++) if ( label[s] == old_label ) label[s] = new_label;
  label_all[ new_label ] = label_all[ old_label ];
  label2header[ new_label ] = label2header[ old_label ];
  
}


double edf_header_t::sampling_freq( const int s ) const
{  
  if ( s < 0 || s >= n_samples.size() ) return -1;
  return n_samples[ s ] / record_duration;
}

std::vector<double> edf_header_t::sampling_freq( const signal_list_t & signals ) const
{
  const int n = signals.size();
  std::vector<double> fs( n );
  for (int s=0;s<n;s++)
    fs[s] = n_samples[ signals.signals[s] ] / record_duration;
  
  return fs;
}


void edf_header_t::check_channels()
{
  // when loading EDF, we would have made unique  (a, a.1, a.2, etc) any identical channel
  // names;  here we also need to check that aliases aren't making non-unique labels
  // e.g. 
  //   A|B,C
  // but EDF has both "B" and "C" 
  // therefore, for each cmd_t::primary_alias[term].vector[] we need to make sure that 
  // we do not see more than one instance

  bool okay = true;

  std::map<std::string,std::vector<std::string> >::const_iterator ii = cmd_t::primary_alias.begin();
  while ( ii != cmd_t::primary_alias.end() )
    {
      std::set<std::string> obs;
      std::vector<std::string>::const_iterator jj = ii->second.begin();
      while ( jj != ii->second.end() )
	{
	  if ( original_signal_no_aliasing( *jj ) != -1 ) obs.insert( *jj );
	  ++jj;
	}
      if ( obs.size() > 1 ) 
	{
	  okay = false;
	  logger << " different channels map to the same alias term: "
		 << ii->first << " <- " << Helper::stringize( obs , " | " ) << "\n";
	}
      ++ii;
    }
  
  if ( ! okay ) 
    Helper::halt( "problem: different channels present in the EDF are mapped to the same alias" );

  
}


bool edf_t::restructure()
{
  
  //
  // Map back onto original epochs
  //
  
  timeline.set_epoch_mapping();

  // output headers
  writer.var( "NR1" , "Number of records prior to restructuring" );
  writer.var( "NR2" , "Number of records after restructuring" );
  writer.var( "DUR1" , "Duration (sec) prior to restructuring" );
  writer.var( "DUR2" , "Duration (sec) after restructuring" );

  // Check that we have anything to do
  
  if ( ! timeline.is_epoch_mask_set() ) 
    {
      
      writer.value( "NR1" , header.nr );
      writer.value( "NR2" , header.nr );

      writer.value( "DUR1" , header.nr * header.record_duration );
      writer.value( "DUR2" , header.nr * header.record_duration );

      return false;
    }
  

  bool any_records_dropped = false;
  int cnt = 0;
  int r = timeline.first_record();
  while ( r != -1 ) 
    {
      if ( timeline.masked_record( r ) )
	{
	  any_records_dropped = true;
	  break;
	}
      ++cnt;
      r = timeline.next_record(r);
    }

  // nothing to do...
  if ( ! any_records_dropped ) 
    {

      writer.value( "NR1" , cnt );
      writer.value( "NR2" , cnt );

      writer.value( "DUR1" , cnt * header.record_duration );
      writer.value( "DUR2" , cnt * header.record_duration );

      return false;

    }


  //
  // We now will have a discontinuous EDF+
  //

  if ( ! header.edfplus ) 
    {
      logger << "  restructuring as an EDF+ : ";
      
      set_edfplus();
    }
      
  set_discontinuous();


  //
  // First, check that all necessary records have actually been loaded
  // This will not reload records, and will only load 'retained' records
  //
  
  // Alternative??  if not found below, just read
  //  read_records( 0 , header.nr_all - 1 );

  //
  // Ensure is loaded if we need it
  //

  std::set<int> include;

  for (int r = 0 ; r < header.nr_all; r++)
    {
      
      bool found     = records.find(r) != records.end();
      bool retained  = timeline.retained(r);
      bool unmasked  = !timeline.masked_record(r);
      
      if ( retained )
	if ( unmasked ) 
	  {
	    if ( ! found ) read_records( r, r );	      
	    include.insert( r );
	  }    
    }

  
  //
  // Remove records based on epoch-mask
  //

  std::map<int,edf_record_t> copy = records;
  records.clear();
  
  //
  // Copy back, but now use iterator instead
  //

  std::set<int>::const_iterator ii = include.begin();
  while ( ii != include.end() )
    {
      records.insert( std::map<int,edf_record_t>::value_type( *ii , copy.find( *ii )->second ) );
      ++ii;
    }


  if ( 0 ) 
    {
      for (int r = 0 ; r < header.nr_all; r++)
	{
	  
	  bool found     = copy.find(r) != copy.end();
	  bool retained  = timeline.retained(r);
	  bool unmasked  = !timeline.masked_record(r);
	  
	  if ( retained )
	    {
	      if ( unmasked ) 
		{	      
		  if ( ! found ) Helper::halt( "internal error in restructure()");
		  records.insert( std::map<int,edf_record_t>::value_type( r , copy.find(r)->second ) );
		  include.insert( r );
		}
	    }
	}
    }

  
      
  // set warning flags, if not enough data left
  
  if ( records.size() == 0 ) globals::problem = true;
    

  logger << "  keeping " 
	 << records.size() << " records of " 
	 << copy.size() << ", resetting mask\n";
  
  writer.value( "NR1" , (int)copy.size() );
  writer.value( "NR2" , (int)records.size() );
  
  writer.value( "DUR1" , copy.size() * header.record_duration );
  writer.value( "DUR2" , records.size() * header.record_duration );

  // update EDF header
  // nb. header.nr_all stays the same, reflecting the 
  // original file which has not changed

  header.nr = records.size();

  // adjust timeline (now will be a discontinuous track)
  
  timeline.restructure( include );

  return true;

}


void edf_t::update_physical_minmax( const int s )
{
  
  interval_t interval = timeline.wholetrace();  
  slice_t slice( *this , s , interval );
  const std::vector<double> * d = slice.pdata();
  const int n = d->size();

  double pmin = (*d)[0];
  double pmax = (*d)[0];
  
  for (int i=1;i<n;i++)
    {
      if      ( (*d)[i] < pmin ) pmin = (*d)[i];
      else if ( (*d)[i] > pmax ) pmax = (*d)[i];
    }
  
  header.physical_min[s] = pmin;
  header.physical_max[s] = pmax;  
  
  // update bitvalue/offset also

  header.bitvalue[s] = ( pmax - pmin ) / (double)( header.digital_max[s] - header.digital_min[s] );
  header.offset[s] = ( pmax / header.bitvalue[s] ) - header.digital_max[s] ;

}


void edf_t::shift( int s , int shift_sp , bool wrap )
{

  if ( shift_sp == 0 ) return;

  // i.e. parameterize as +ve means to push the series forward
  shift_sp = -shift_sp;
  
  // get data : note, this ignores EDF discontinuities
  
  slice_t slice( *this , s , timeline.wholetrace() );

  const std::vector<double> * d = slice.pdata();
  
  const int np = d->size();

  if ( np <= shift_sp ) return;
  
  std::vector<double> d2( np , 0 );
  
  for (int i=0;i<np;i++)
    {

      int j = i - shift_sp;
      
      if ( j < 0 )
	{
	  if ( wrap ) 
	    {
	      j = np - shift_sp + i;
	      d2[j] = (*d)[i];
	    }
	}
      else if ( j >= np ) 
	{ 
	  if ( wrap ) 
	    {
	      j = j - np;
	      d2[j] = (*d)[i];
	    }
	}
      else
	{
	  d2[j] = (*d)[i];
	}
    }
  
  update_signal( s , &d2 );

}


void edf_t::copy_signal( const std::string & from_label , const std::string & to_label )
{
  
  const int s1 = header.signal( from_label );
  
  if ( s1 == -1 ) 
    Helper::halt( "could not find signal " + from_label );
  
  if ( header.has_signal( to_label ) ) 
    Helper::halt( to_label + " already exists in the EDF" );
  

  //
  // get data
  //

  interval_t interval = timeline.wholetrace();  
  slice_t slice( *this , s1 , interval );
  const std::vector<double> * d = slice.pdata();
  
  //
  // add signal
  //

  add_signal( to_label , header.sampling_freq(s1) , *d );
  
  //
  // and copy the header values that would not have been properly set by add_signal()
  //

  const int s2 = header.signal( to_label );
  
  if ( s2 == -1 ) 
    Helper::halt( "problem with COPY: could not find new signal " + to_label );
  
  header.transducer_type[s2] = header.transducer_type[s1];
  header.phys_dimension[s2] = header.phys_dimension[s1];
  header.prefiltering[s2] = header.prefiltering[s1];
  
}


void edf_t::update_records( int a , int b , int s , const std::vector<double> * d )
{

  if ( header.is_annotation_channel(s) ) 
    Helper::halt( "edf_t:: internal error, cannot update an annotation channel" );

  // keep digital min/max scale as is.

  // for signal s, place back data in 'd' into EDF record structure
  // and update the physical min/max

  const int points_per_record = header.n_samples[s];
  const int n_records = b - a + 1 ;

  //std::cerr << "a,b = " << a << " " << b << " " << header.nr << "\n";

  if ( a < 0 || b < 0 || n_records <= 0 || a >= header.nr_all || b >= header.nr_all )    
    Helper::halt( "bad record specification in edf_t::update_records()" );
  const int n = d->size();
  
  if ( n != n_records * points_per_record )
    Helper::halt( "internal error in update_records()" );
  
  // use existing digital/physical min/max encoding
  // but will need to make sure we stay within digital min/max
  
  const int16_t dmin = header.digital_min[s];
  const int16_t dmax = header.digital_max[s];
  
  double pmin = header.physical_min[s];
  double pmax = header.physical_max[s];
  
  double bv = header.bitvalue[s];
  double os = header.offset[s];
  
  int cnt = 0;
  
  // assume records have already been read in... if they have, this function
  // automatically returns so okay to call just in case

  read_records( a , b );

  for ( int r = a ; r <= b ; r++ ) 
    {
      
      // find records      
      std::vector<int16_t>    & data  = records.find(r)->second.data[ s ];
      
      // check that we did not change sample rate      
      if ( data.size() != points_per_record ) 
	Helper::halt( "changed sample rate, cannot update record" );
      
      for (int p=0;p<points_per_record;p++)
	{
	  double x = (*d)[cnt];
	  if ( x < pmin ) x = pmin;
	  else if ( x > pmax ) x = pmax;

// 	  std::cout << "edit\t" << edf_record_t::dig2phys( data[p] , bv , os ) 
// 		    << "\t"
// 		    << (*d)[cnt] << "\n";
	  data[p] = edf_record_t::phys2dig( (*d)[cnt] , bv , os );
	  ++cnt;	  
	}
    }
}

  

void edf_t::update_signal( int s , const std::vector<double> * d , bool force_minmax )
{
  
  if ( header.is_annotation_channel(s) ) 
    Helper::halt( "edf_t:: internal error, cannot update an annotation channel" );
  
  // for signal s, place back data in 'd' into EDF record structure
  // and update the physical min/max

  const int points_per_record = header.n_samples[s];
  const int n = d->size();

  if ( n != header.nr * points_per_record )
    Helper::halt( "internal error in update_signal()" );

  // use full digital min/max scale
  const int16_t dmin = -32768;
  const int16_t dmax = 32767;  
  header.digital_min[s] = dmin;
  header.digital_max[s] = dmax;

  double pmin = (*d)[0];
  double pmax = (*d)[0];
  
  for (int i=0;i<n;i++)
    {
      if      ( (*d)[i] < pmin ) pmin = (*d)[i];
      else if ( (*d)[i] > pmax ) pmax = (*d)[i];
    }

  // force equal physical min/max in EDF?
  if ( force_minmax )
    {
      double largest = fabs( pmin );
      if ( fabs( pmax ) > largest ) largest = fabs( pmax ) ;
      pmin = -largest;
      pmax =  largest;
    }
  
  // update physical min/max (but leave orig_physical_min/max unchanged)
  header.physical_min[s] = pmin;
  header.physical_max[s] = pmax;

  double bv = ( pmax - pmin ) / (double)( dmax - dmin );
  double os = ( pmax / bv ) - dmax;

  // TODO: we should flag in the header that this signal has been transformed
  // (?always) we expect that all data will have been read, so no more will be read
  // from disk;  if it were, these could be the incorrect BV/OS values, of course. 
  // We should set a flag that header has changed, and bail out if subsequent reads are made

  header.bitvalue[s] = bv;
  header.offset[s] = os;
  
  int cnt = 0;
  
  int r = timeline.first_record();
  while ( r != -1 ) 
    {
      
      // find records

      //      std::vector<double> & pdata = records.find(r)->second.pdata[ s ];
      std::vector<int16_t>    & data  = records.find(r)->second.data[ s ];
      
      // check that we did not change sample rate
      
      if ( data.size() != points_per_record ) 
	{
	  //pdata.resize( points_per_record , 0 );
	  data.resize( points_per_record , 0 );
	}

      
      for (int p=0;p<points_per_record;p++)
	{
	  
	  //pdata[p] = (*d)[cnt]; 
	  
	  // also need to convert to bit-value
	  // pdata[s][j] = header->bitvalue[s] * ( header->offset[s] + d );	  
	  // reverse digital --> physical scaling
	  
	  //data[p] = (*d)[cnt] / bv - os;
	  data[p] = edf_record_t::phys2dig( (*d)[cnt] , bv , os );
	
	  ++cnt;	  
	}

      r = timeline.next_record(r);
    }
}




edf_record_t::edf_record_t( edf_t * e ) 
{    

  edf = e;

  // only store digital value, convert on-the-fly
  data.resize( edf->header.ns );
  //pdata.resize( edf->header.ns );
  
  for (int s = 0 ; s < edf->header.ns ; s++)
    {
      if ( edf->header.is_annotation_channel(s) )
	{
	  data[s].resize( 2 * edf->header.n_samples[s] , 0 );
	  //pdata[s].resize( edf->header.n_samples[s] * fac , 0 );
	}
      else
	{
	  data[s].resize( edf->header.n_samples[s] , 0 );
	  //pdata[s].resize( edf->header.n_samples[s] , 0 );
	}
    }
  
}


void edf_t::reset_start_time()
{

  // get time of first record
  int r = timeline.first_record();
  if ( r == -1 ) return;
  // interval for this record
  logger << "  resetting EDF start time from " << header.starttime ;

  interval_t interval = timeline.record2interval(r); 

  clocktime_t et( header.starttime );
  if ( et.valid )
    {
      double time_hrs = ( interval.start * globals::tp_duration ) / 3600.0 ; 
      et.advance( time_hrs );
    }

  header.starttime = et.as_string();;
  logger << " to " << header.starttime  << "\n"; 
}


void edf_t::set_continuous()
{
  if ( ! header.edfplus ) return;
  header.continuous = true;
  header.reserved[4] = 'C';    

}

void edf_t::set_discontinuous()
{
  if ( ! header.edfplus ) return;
  header.continuous = false;
  header.reserved[4] = 'D';    
}


void edf_t::set_edfplus()
{

  if ( header.edfplus ) return;
  header.edfplus = true;
  header.continuous = true;  
  header.reserved[0] = 'E'; 
  header.reserved[1] = 'D';
  header.reserved[2] = 'F';
  header.reserved[3] = '+';
  
  set_continuous();
  add_continuous_time_track();
}

void edf_t::set_edf()
{
  if ( ! header.edfplus ) return;
  header.edfplus = false;
  header.continuous = true;  
  header.reserved[0] = ' '; 
  header.reserved[1] = ' ';
  header.reserved[2] = ' ';
  header.reserved[3] = ' ';

  set_continuous(); 
  drop_time_track();
}

void edf_t::drop_time_track()
{
  // means that the EDF will become 'continuous'
  set_continuous();

  // no TT in any case?
  if ( header.time_track() == -1 ) return;
  drop_signal( header.time_track() );
  
}


int edf_t::add_continuous_time_track()
{
  
  // this can only add a time-track to a continuous record
  // i.e. if discontinuous, it must already (by definition) 
  // have a time track

  if ( ! header.continuous ) 
    return header.time_track();

  if ( ! header.edfplus ) set_edfplus();

  // time-track already set?
  if ( header.time_track() != -1 ) return header.time_track();

  // update header
  ++header.ns;

  // set t_track channel
  header.t_track  = header.ns - 1;
  header.t_track_edf_offset = record_size; // i.e. at end of record

  const int16_t dmax = 32767;
  const int16_t dmin = -32768;
  
  // need to set a record size -- this should be enough?
  const int n_samples = globals::edf_timetrack_size;

  // how many existing 'EDF Annotations' tracks?
  int annot_tracks = 0 ; 

  std::map<std::string,int>::const_iterator jj = header.label_all.begin();
  while ( jj != header.label_all.end() )
    {
      if ( Helper::imatch( jj->first  , "EDF Annotation" , 14 ) ) 
	annot_tracks++;                     
      ++jj;
    }

  header.label.push_back( "EDF Annotations" + ( annot_tracks > 0 ? Helper::int2str( annot_tracks ) : "" ) );
  header.annotation_channel.push_back( true );

  // note: annot, so not added to header/record signal map label2header

  header.transducer_type.push_back( "" );
  header.phys_dimension.push_back( "" );

  header.physical_min.push_back( 0 ); // ignored
  header.physical_max.push_back( 1 ); // ignored
  header.digital_min.push_back( dmin );
  header.digital_max.push_back( dmax );

  header.orig_physical_min.push_back( 0 ); // ignored
  header.orig_physical_max.push_back( 1 ); // ignored
  header.orig_digital_min.push_back( dmin );
  header.orig_digital_max.push_back( dmax );

  header.prefiltering.push_back( "" );
  header.n_samples.push_back( n_samples );
  header.signal_reserved.push_back( "" );  
  header.bitvalue.push_back( 1 ); // ignored
  header.offset.push_back( 0 );   // ignored
  
  // create each 'TAL' timestamp, and add to record
  double dur_sec = header.record_duration;
  double onset = 0; // start at T=0

  uint64_t onset_tp = 0;
  uint64_t dur_tp = header.record_duration_tp;

  // for each record
  int r = timeline.first_record();
  
  while ( r != -1 ) 
    {

      std::string ts = "+" + Helper::dbl2str( onset ) + "\x14\x14\x00";
      
      // need to make sure that the record (i.e. other signals) 
      // are first loaded into memory...
      
      bool record_in_memory = loaded(r);
      
      if ( ! record_in_memory )
	{

	  // this will be created with ns+1 slots (i.e. 
	  // already with space for the new timetrack, 
	  // so we can add directly)
	  
	  edf_record_t record( this ); 
	  
	  record.read( r );

	  records.insert( std::map<int,edf_record_t>::value_type ( r , record ) );

	}

	  
      //
      // Add the time-stamp as the new track (i.e. if we write as EDF+)
      //
      
      if ( ! record_in_memory ) // record already 'updated'
	records.find(r)->second.add_annot( ts , header.t_track );
      else // push_back on end of record
	records.find(r)->second.add_annot( ts );
      
      //
      // And mark the actual record directy (i.e. if this is used in memory)
      //
      
      onset += dur_sec;
      onset_tp += dur_tp;
     
      r = timeline.next_record(r);
    }

//   std::cout << "DET1 " << records.begin()->second.pdata.size() << "\t"
// 	    << records.begin()->second.data.size() << "\n";


  return header.time_track();

}




uint64_t edf_t::timepoint_from_EDF( int r )
{
  
  //
  // Read this is called when constructing a time-series for 
  // an existing EDF+D, only
  //

  if ( ! header.edfplus ) Helper::halt( "should not call timepoint_from_EDF for basic EDF");
  if (   header.continuous ) Helper::halt( "should not call timepoint_from_EDF for EDF+C");
  if (   header.time_track() == -1 ) Helper::halt( "internal error: no EDF+D time-track" );
  
  // determine offset into EDF
  uint64_t offset = header_size + (uint64_t)(record_size) * r;

  offset += header.time_track_offset(); 

  // time-track is record : edf->header.time_track 
  // find the appropriate record
  fseek( file , offset , SEEK_SET );
  
  int ttsize = 2 * globals::edf_timetrack_size;
  
  // allocate space in the buffer for a single record, and read from file
  byte_t * p = new byte_t[ ttsize ];
  byte_t * p0 = p;

  // and read only time-track (all of it)
  size_t rdsz = fread( p , 1, ttsize , file );
  
  std::string tt( ttsize , '\x00' );
  int e = 0;
  for (int j=0; j < ttsize; j++)
    {      
      tt[j] = *p;
      if ( tt[j] == '\x14' || tt[j] == '\x15' ) break;
      ++p;
      ++e;
    }

  double tt_sec = 0;

  if ( ! Helper::str2dbl( tt.substr(0,e) , &tt_sec ) ) 
    Helper::halt( "problem converting time-track in EDF+" );

  delete [] p0;
  
  uint64_t tp = globals::tp_1sec * tt_sec;

  return tp; 

}
  
void edf_t::flip( const int s )
{
  if ( header.is_annotation_channel(s) ) return;
  logger << "  flipping polarity of " << header.label[s] << "\n";

  // get all data
  interval_t interval = timeline.wholetrace();
  slice_t slice( *this , s , interval );
  const std::vector<double> * d = slice.pdata();
  std::vector<double> rescaled( d->size() );
  
  for (int i=0;i<d->size();i++)  rescaled[i] = - (*d)[i];

  // update signal (and min/max in header)
  update_signal( s , &rescaled );
  
}


void edf_t::rescale( const int s , const std::string & sc )
{
  
  if ( header.is_annotation_channel(s) ) return;

  bool is_mV = header.phys_dimension[s] == "mV";
  bool is_uV = header.phys_dimension[s] == "uV";
  bool is_V  = header.phys_dimension[s] == "V";

  bool rescale_from_mV_to_uV = is_mV && sc == "uV"; // *1000
  bool rescale_from_uV_to_mV = is_uV && sc == "mV"; // /1000
  
  bool rescale_from_V_to_uV = is_V && sc == "uV"; // * 1e6
  bool rescale_from_V_to_mV = is_V && sc == "mV"; // * 1e3

  if ( ! ( rescale_from_mV_to_uV || rescale_from_uV_to_mV 
	   || rescale_from_V_to_uV || rescale_from_V_to_mV ) ) 
    {
      //logger << " no rescaling needed\n";
      return;
    }

  // get all data
  interval_t interval = timeline.wholetrace();
  slice_t slice( *this , s , interval );
  const std::vector<double> * d = slice.pdata();
  std::vector<double> rescaled( d->size() );

  // get rescaling factor
  double fac = 1;
  if      ( rescale_from_uV_to_mV ) fac = 1.0/1000.0;
  else if ( rescale_from_mV_to_uV ) fac = 1000;
  else if ( rescale_from_V_to_mV )  fac = 1000;
  else if ( rescale_from_V_to_uV )  fac = 1000000;
  
  // rescale
  for (int i=0;i<d->size();i++) 
    {
      rescaled[i] = (*d)[i] * fac;
    }

  // update signal (and min/max in header)
  update_signal( s , &rescaled );

  // update headers
  if ( rescale_from_mV_to_uV || rescale_from_V_to_uV ) 
    {
      logger << " rescaled " << header.label[s] << " to uV\n";
      header.phys_dimension[s] = "uV";     
    }
  
  if ( rescale_from_uV_to_mV || rescale_from_V_to_mV ) 
    {
      logger << " rescaled " << header.label[s] << " to mV\n";
      header.phys_dimension[s] = "mV";
    }
}



void edf_t::minmax( signal_list_t & signals )
{

  // // get max/min for digital and physical signals over all 's' in signals
  // if ( header.is_annotation_channel(s) ) return;

  // logger << "  forcing EDF min/max to be similar for " << header.label[s] << "\n";

  // // get all data
  // interval_t interval = timeline.wholetrace();
  // slice_t slice( *this , s , interval );
  // const std::vector<double> * d = slice.pdata();

  // // update signal (and min/max in header), where true implies
  // // we force the same physical min/max values in the EDF header
  // update_signal( s , d , true );
}


bool edf_t::basic_stats( param_t & param )
{
  
  // Run through each record
  // Get min/max
  // Calculate RMS for each signal
  // Get mean/median/SD and skewness
  // optinoally, display a histogram of observed values (and figure out range)
  
  std::string signal_label = param.requires( "sig" );  

  signal_list_t signals = header.signal_list( signal_label );

  std::vector<double> Fs = header.sampling_freq( signals );
  
  bool by_epoch = param.has( "epoch" );

  bool hist = param.has( "encoding" );
  
  const int ns = signals.size();
  
  bool calc_median = true;

  for (int s=0; s<ns; s++)
    {
               
      //
      // skip annotation channels
      //

      if ( header.is_annotation_channel( signals(s) ) ) continue;


      //
      // Output signal
      //
      
      writer.level( header.label[ signals(s) ] , globals::signal_strat );

      //
      // Mean, variance, skewmess, RMS, min, max based on per-epoch stats
      //
            
      std::vector<double> e_mean, e_median , e_sd, e_rms, e_skew;
      
      double t_min = 0 , t_max = 0;
      
      logger << " processing " << header.label[ signals(s) ] << " ...\n";
      
      
      //
      // EPOCH-level statistics first
      //
      
      if ( by_epoch ) 
	{

	  timeline.first_epoch();  
	  	  
	  //
	  // Iterate over epcohs
	  //
	  
	  while ( 1 ) 
	    {
	      
	      int epoch = timeline.next_epoch();      
	      
	      if ( epoch == -1 ) break;
	      
	      interval_t interval = timeline.epoch( epoch );
	      
	      //
	      // Get data 
	      //
	      
	      slice_t slice( *this , signals(s) , interval );
	      
	      const std::vector<double> * d = slice.pdata();
	      
	      const int n = d->size();
	      
	      if ( n == 0 ) { continue; } 
	      
	      
	      //
	      // Filter data
	      //
	      
	      double mean   = MiscMath::mean( *d );
	      double median = calc_median ? MiscMath::median( *d ) : 0;
	      double sd     = MiscMath::sdev( *d , mean );
	      double rms    = MiscMath::rms( *d );
	      double skew   = MiscMath::skewness( *d , mean , sd );
					     
	      double min = (*d)[0];
	      double max = (*d)[0];
	      
	      for (int i = 0 ; i < n ; i++ )
		{
		  if ( (*d)[i] < min ) min = (*d)[i];
		  if ( (*d)[i] > max ) max = (*d)[i];
		}
	      
	      
	      //
	      // Output
	      //
	      
	      writer.epoch( timeline.display_epoch( epoch ) );
	      
	      writer.value( "MAX"  , max  );
	      writer.value( "MIN"  , min  );	      
	      writer.value( "MEAN" , mean );
	      writer.value( "SKEW" , skew );

	      if ( calc_median ) 
		writer.value( "MEDIAN" , median );	      

	      writer.value( "RMS"  , rms  );
	      
	      //
	      // Record
	      //
	      
	      if ( t_min == 0 && t_max == 0 ) 
		{ 
		  t_min = min; 
		  t_max = max; 
		} 
	      
	      if ( min < t_min ) t_min = min;
	      if ( max > t_max ) t_max = max;
	      
	      e_mean.push_back( mean );
	      if ( calc_median ) 
		e_median.push_back( median );
	      e_sd.push_back( sd );
	      e_rms.push_back( rms );	  
	      e_skew.push_back( skew );
	    }
	  
	  writer.unepoch();
	  
	  
	}
      
      
      //
      // Whole-signal level output
      //
      
      interval_t interval = timeline.wholetrace();
      
      slice_t slice( *this , signals(s) , interval );
	  
      const std::vector<double> * d = slice.pdata();
      
      const int n = d->size();

      if ( n == 0 ) { continue; } 
 
      double mean = MiscMath::mean( *d );
      double median = calc_median ? MiscMath::median( *d ) : 0 ;
      double sd = MiscMath::sdev( *d );
      double rms  = MiscMath::rms( *d );
      double skew = MiscMath::skewness( *d , mean , sd );
				    
      double min = (*d)[0];
      double max = (*d)[0];
      
      for (int i = 0 ; i < n ; i++ )
	{
	  if ( (*d)[i] < min ) min = (*d)[i];
	  if ( (*d)[i] > max ) max = (*d)[i];
	}

      
      //
      // Output
      //
	  

      writer.value( "MAX"  , max  );
      writer.value( "MIN"  , min  );      
      writer.value( "MEAN" , mean );
      writer.value( "SKEW" , skew );
      if ( calc_median ) writer.value( "MEDIAN" , median );
      writer.value( "RMS"  , rms  );

      
      //
      // Also, same strata:  summaries of epoch-level statistics
      //
      
      if ( by_epoch && e_mean.size() > 0 )
	{
	  const int ne = e_mean.size(); 
	  double med_mean  = median_destroy( &e_mean[0] , ne );
	  double med_median  = calc_median ? median_destroy( &e_median[0] , ne ) : 0 ;  
	  double med_rms  = median_destroy( &e_rms[0] , ne );
	  double med_skew = median_destroy( &e_skew[0] , ne );
	  
	  writer.value( "NE" , timeline.num_total_epochs() );	  
	  writer.value( "NE1" , ne );

	  writer.value( "MEDIAN.MEAN" , med_mean );
	  if ( calc_median )
	    writer.value( "MEDIAN.MEDIAN" , med_median );
	  writer.value( "MEDIAN.RMS"  , med_rms );
	  writer.value( "MEDIAN.SKEW" , med_skew );
	}



      //
      // Optional, encoding 
      //
      
      // verbose output: every unique value / count 
      if ( hist )
	{
	  
	  std::map<double,int> counts;
	  for (int i = 0 ; i < n ; i++ )
	    counts[ (*d)[i] ]++;
	  
	  writer.value( "OBS_ENCODING" , (int)counts.size() );
	  
	  // largest possible EDF digital span
	  
	  // -32767 for "digital minimum" and +32767 for "digital maximum"? 
	  //	  int span_max = 32767 - ( -32767 );
	  
	  int span_obs = header.digital_max[ signals(s) ] - header.digital_min[ signals(s) ] + 1;
	  
	  int zero_cells = span_obs - counts.size();
	  
	  writer.value( "MAX_ENCODING" , span_obs );
	  writer.value( "PCT_ENCODING" , counts.size() / (double)span_obs );
	  
	  std::map<double,int>::const_iterator ii = counts.begin();
	  while ( ii != counts.end() )
	    {
	      writer.level( ii->first , globals::value_strat );
	      writer.value( "CNT" , ii->second );
	      ++ii;
	    }
	  writer.unlevel( "VAL" );

	}
      
      //
      // Next channel
      //
            
    }

  //
  // All done
  //

  writer.unlevel( globals::signal_strat );
  
  return true;
  
}




bool signal_list_t::match( const std::set<std::string> * inp_signals ,
			   std::string * l ,
			   const std::set<std::string> & slabels )
{
    
  // exact match? (i.e. no "|" alternatives specified)
  if ( inp_signals->find(*l) != inp_signals->end() ) return true; 
  
  // as an alias?
  if ( cmd_t::label_aliases.find( *l ) != cmd_t::label_aliases.end() )
    {
      *l = cmd_t::label_aliases[ *l ];
      return inp_signals->find(*l) != inp_signals->end() ;
    }
  
  // subset match (i.e. one of x|y|z)
  // if both 'x' and 'y' exist, always pick 'x' first
  
  std::set<std::string>::const_iterator ii = inp_signals->begin();
  while ( ii != inp_signals->end() )
    {
      std::vector<std::string> tok = Helper::parse( *ii , "|" );
      for (int i=0;i<tok.size();i++) 
	{
	  
	  // if gone preferred value exists in some other slot, then this is not a match
	  // i.e. only include one selection, the preferred one
	  if ( i>0 && slabels.find( tok[0] ) != slabels.end() )  break; 
	    
	  if ( *l == tok[i] ) 
	    {
	      // swap in 'preferred' name
	      if ( i>0 ) *l = tok[0];
	      return true;
	    }
	}
      ++ii;
    }    
  return false;
}





void edf_t::make_canonicals( const std::string & file0, const std::string &  group , const std::set<std::string> * cs )
{

  std::string file = Helper::expand( file0 );
  
  if ( ! Helper::fileExists( file ) )
    Helper::halt( "could not find " + file );
  
  // GROUP   CANONICAL   CH   REF   SR  NOTES
  // looking for EEG, LOC, ROC, EMG, ECG

  // if cs is non-null, only make the CS in that set ('EEG')

  // can have multiple versions of a rule, will pick the first match

  //  EEG   C4,EEG1      M1,A1   100  
  //  EEG   C3,EEG2      M2,A2   100  Using C3, not C4
  //  EEG   C4_A1,C4_M1  .       100
  //  EEG   C3_A2,C3_M2  .       100  Using C3, not C4

  std::map<std::string,std::vector< std::vector<std::string> > >sigs, refs;
  std::map<std::string,std::vector<std::string> > srs, notes;
  
  std::ifstream IN1( file.c_str() , std::ios::in );
  while ( ! IN1.eof() )
    {
      std::string line;
      Helper::safe_getline( IN1 , line );
      if ( line == "" ) continue;
      if ( IN1.eof() ) break;
      if ( line[0] == '%' ) continue;
      std::vector<std::string> tok = Helper::parse( line , "\t" );
      if ( tok.size() != 5 && tok.size() != 6 ) 
	Helper::halt( "bad format, expecting 5 or 6 tab-delimited columns\nfile: " 
		      + file + "\nline: [" + line + "]\n" );
      if ( tok[0] != group ) continue;

      // skip if a specific list requested?
      if ( cs != NULL && cs->find( tok[1] ) == cs->end() ) continue;

      // otherwise, add to the set of things to be calculated
      sigs[ "cs_" + tok[1] ].push_back( Helper::parse( tok[2] , "," ) );
      refs[ "cs_" + tok[1] ].push_back( Helper::parse( tok[3] , "," ) );
      srs[ "cs_" + tok[1] ].push_back( tok[4] ) ;
      notes[ "cs_" + tok[1] ].push_back( tok.size() == 6 ? tok[5] : "." );
      
    }
  

  //
  // Set to sample rate SR if not already done, and ensure units are uV
  //

  std::vector<std::string> canons;

  if ( cs == NULL ) 
    {
      canons.push_back( "cs_EEG" );
      canons.push_back( "cs_LOC" );
      canons.push_back( "cs_ROC" );
      canons.push_back( "cs_EMG" );
      canons.push_back( "cs_ECG" );
    }
  else
    {
      std::set<std::string>::const_iterator ss = cs->begin();
      while ( ss != cs->end() )
	{
	  canons.push_back( "cs_" + *ss );
	  ++ss;
	}
    }



  for (int i=0; i<canons.size(); i++)
    {
      std::string canon = canons[i];

      writer.level( canon , "CS" );

      if ( sigs.find( canon ) == sigs.end() )
	{
	  writer.value( "DEFINED" , 0 );
	  continue;
	}

      // as soon as we find a matching rule, we stop
      bool done = false;
      
      const int n_rules = sigs.find( canon )->second.size() ; 
          
      for (int j=0; j<n_rules; j++ )
	{
	  
	  // find best choice of signals
	  std::string sigstr = "";
	  std::vector<std::string> v = sigs.find( canon )->second[j];
	  for (int k=0; k<v.size(); k++)
	    {
	      if ( header.signal( v[k] ) != -1 ) 
		{
		  sigstr = v[k];
		  break;
		}
	    }
	  
	  if ( sigstr == "" ) 
	    {
	      //writer.value( "DEFINED" , 0 );
	      continue;
	    }

	  //
	  // Reference
	  //
	  
	  std::string refstr = "";
	  v = refs.find( canon )->second[j];
	  if ( v.size() == 1 && v[0] == "." ) 
	    refstr = ".";
	  else
	    {
	      for (int k=0; k<v.size(); k++)
		{
		  if ( v[k] == "." ) 
		    Helper::halt( "cannot mix '.' and non-'.' references" );
		  
		  if ( header.signal( v[k] ) != -1 )
		    {
		      refstr = v[k];
		      break;
		    }
		}
	    }
	  
	  if ( sigstr == "" || refstr == "" )
	    {
	      //writer.value( "DEFINED" , 0 );
	      continue;
	    }      

	  logger << "  generating canonical signal " << canon 
		 << " from " << sigstr << "/" << refstr << "\n";
	  	  
	  std::string srstr = srs.find( canon )->second[j];

	  std::string notesstr = notes.find( canon )->second[j];
	    
	  int sr = 0;
	  if ( ! Helper::str2int( srstr , &sr ) )
	    Helper::halt( "could not determine integer SR from " + file );
	  

	  // copy signal --> canonical form
	  signal_list_t ref;
	  if ( refstr != "." ) ref =  header.signal_list( refstr );
	  
	  signal_list_t sig = header.signal_list( sigstr );

	  //
	  // Rerefence and make canonical signal
	  //

	  reference( sig , ref , true , canon , sr );
	  
	  signal_list_t canonical_signal = header.signal_list( canon );

	  
	  //
	  // rescale units?
	  //
	  
	  // EEG, EOG, EMG all uV
	  // ECG in mV
	  // others?
	  
	  std::string units = "uV";
	  
	  if ( canon == "cs_ECG" ) units = "mV" ;
	  
	  if ( units == "uV" || units == "mV" ) 
	    rescale(  canonical_signal(0) , units );

      
	  //
	  // output
	  //
	  
	  writer.value( "DEFINED" , 1 );
	  writer.value( "SIG" , sigstr );
	  writer.value( "REF" , refstr );
	  writer.value( "SR" , srstr );
	  writer.value( "UNITS" , units );
	  
	  if ( notesstr != "" )
	    writer.value( "NOTES" , notesstr );
	 
	  
	  //
	  // at this point, rule was found, so quit
	  //

	  break;

	} // next rule for this CS

      // if we failed to get a match, flag here

      if ( ! done ) 
	writer.value( "DEFINED" , 0 );
      
    } // next CS
  
  writer.unlevel( "CS" );
  
}
