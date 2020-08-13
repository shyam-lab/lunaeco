

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

#include "annot.h"
#include "eval.h"
#include "edf/edf.h"
#include "helper/helper.h"
#include "helper/logger.h"
#include "defs/defs.h"
#include "tinyxml/xmlreader.h"
#include "db/db.h"
#include "nsrr-remap.h"
#include "helper/token-eval.h"

#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>

extern writer_t writer;

extern logger_t logger;

extern globals global;


void annot_t::wipe()
{
  std::set<instance_t *>::iterator ii = all_instances.begin();
  while ( ii != all_instances.end() )
    {	 
      if ( *ii != NULL ) 
	delete *ii;
      ++ii;
    }    
  all_instances.clear();
}


instance_t * annot_t::add( const std::string & id , const interval_t & interval )
{

  // swap in hh:mm:ss for null instance ID?
  
  bool id2hms = 
    globals::set_annot_inst2hms_force ||
    ( globals::set_annot_inst2hms &&
      ( id == "." || id == "" || id == name ) );

  std::string id2 = id;
  
  if ( id2hms )
    {
      clocktime_t t = parent->start_ct ;
      double start_hrs = interval.start_sec() / 3600.0; 
      t.advance( start_hrs );
      id2 = t.as_string();
    }
      
  instance_t * instance = new instance_t ;
  
  // track (for clean-up)
  all_instances.insert( instance );
    
  interval_events[ instance_idx_t( this , interval , id2 ) ] = instance; 
  
  return instance; 
  
}

void annot_t::remove( const std::string & id , const interval_t & interval )
{

  instance_idx_t key = instance_idx_t( this , interval , id );

  std::map<instance_idx_t,instance_t*>::iterator ii = interval_events.find( key );

  if ( ii == interval_events.end() ) return;

  // clean up instance
  if ( ii->second != NULL ) {
    
    // remove pointer from global instance tracker
    std::set<instance_t*>::iterator kk = all_instances.find( ii->second );
    if ( kk != all_instances.end() )
      all_instances.erase( kk );

    // release actual instance
    delete ii->second;
  }
  
  // clean up idx
  interval_events.erase( key );

}


std::string instance_t::print( const std::string & delim , const std::string & prelim ) const
{
  std::stringstream ss;

  std::map<std::string,avar_t*>::const_iterator dd = data.begin();
  while ( dd != data.end() )
    {
      
      if ( dd != data.begin() ) ss << delim;

      ss << prelim;
      
      if ( dd->second == NULL )
	ss << dd->first;
      else if ( dd->second->atype() == globals::A_BOOLVEC_T )
	ss << dd->first << "=" << Helper::stringize( dd->second->text_vector() , "," ) ;
      else if ( dd->second->atype() == globals::A_INTVEC_T )
	ss << dd->first << "=" << Helper::stringize( dd->second->int_vector() , "," ) ;
      else if ( dd->second->atype() == globals::A_DBLVEC_T )
	ss << dd->first << "=" << Helper::stringize( dd->second->double_vector() , "," ) ;
      else if ( dd->second->atype() == globals::A_TXTVEC_T )
	ss << dd->first << "=" << Helper::stringize( dd->second->text_vector() , "," ) ;

      else
 	ss << dd->first << "=" << dd->second->text_value();
      ++dd;

    }

  return ss.str();
}

globals::atype_t instance_t::type( const std::string & s ) const 
{
  std::map<std::string,avar_t*>::const_iterator ii = data.find( s );
  if ( ii == data.end() ) return globals::A_NULL_T;  
  return ii->second->atype();
}

void instance_t::check( const std::string & name )
{

  std::map<std::string,avar_t*>::iterator dd = data.find( name );

  if ( dd == data.end() ) return;

  if ( dd->second == NULL ) return;  // flag, so no storage set
  
  // erase actual storage...
  delete dd->second; 

  // erase place in the tracker
  std::set<avar_t*>::iterator ff = tracker.find( dd->second );
  if ( ff != tracker.end() )
    tracker.erase( tracker.find( dd->second ) ); 
  else 
    Helper::halt( "internal error in instance_t::check()... avar_t not tracked" );
  
  // and erase from this data map instance
  data.erase( dd );  
  
  return;
}

void instance_t::set( const std::string & name ) 
{
  check( name );
  avar_t * a = new flag_avar_t ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const int i ) 
{
  check( name );
  avar_t * a = new int_avar_t( i ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const std::string & s ) 
{
  check( name );
  avar_t * a = new text_avar_t( s ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const bool b )
{
  check( name );
  avar_t * a = new bool_avar_t( b ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set_mask( const std::string & name , const bool b )
{
  check( name );
  avar_t * a = new mask_avar_t( b ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const double d )
{
  check( name );
  avar_t * a = new double_avar_t( d ) ;
  tracker.insert( a );
  data[ name ] = a;    
}


// vectors
void instance_t::set( const std::string & name , const std::vector<int> &  i ) 
{
  check( name );
  avar_t * a = new intvec_avar_t( i ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const std::vector<std::string> & s ) 
{
  check( name );
  avar_t * a = new textvec_avar_t( s ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const std::vector<bool> & b )
{
  check( name );
  avar_t * a = new boolvec_avar_t( b ) ;
  tracker.insert( a );
  data[ name ] = a;    
}

void instance_t::set( const std::string & name , const std::vector<double> & d )
{
  check( name );
  avar_t * a = new doublevec_avar_t( d ) ;
  tracker.insert( a );
  data[ name ] = a;    
}



instance_t::~instance_t()
{
  std::set<avar_t *>::iterator ii = tracker.begin();
  while ( ii != tracker.end() )
    {	 
      delete *ii;
      ++ii;
    }
}  

std::ostream & operator<<( std::ostream & out , const avar_t & a )
{
  out << a.text_value();
  return out;
}


void summarize_annotations( edf_t & edf , param_t & param )
{
  
  writer.var( "ANNOT_N" , "Number of occurrences of an annotation" );
  
  std::map<std::string,int>::const_iterator ii = edf.aoccur.begin();
  while ( ii != edf.aoccur.end() ) 
    {
      // annot as 'level'
      writer.level( ii->first , globals::annot_strat );
      writer.value( "ANNOT_N" , ii->second );
      ++ii;
    }
}


bool annot_t::map_epoch_annotations(   edf_t & parent_edf , 
				       const std::vector<std::string> & ann , 
				       const std::string & filename , 
				       uint64_t elen , 
				       uint64_t einc )
{


  // static function that will create multiple annotations (one per class label)
    
  bool unepoched = elen == 0 ;
  
  if ( unepoched )  
    {
      elen = Helper::sec2tp( globals::default_epoch_len );
      einc = Helper::sec2tp( globals::default_epoch_len );
    }

  // get implied number of epochs
  double seconds = (uint64_t)parent_edf.header.nr * parent_edf.header.record_duration ;      
  const int ne = seconds / ( unepoched ? globals::default_epoch_len : elen / globals::tp_1sec );
  
  if ( globals::enforce_epoch_check )
    {
      if ( ne != ann.size() ) 
	Helper::halt( "expecting " + Helper::int2str(ne) + " epoch annotations, but found " + Helper::int2str( (int)ann.size() ) );
    }
  
  
  //
  // because we otherwise might have a discontinuous EDF, we need to look up the proper epoch 
  // intervals below , if epoched 

  
  //
  // map of all labels/annot classes to be added
  //

  std::map<std::string,annot_t*> amap;

  for (int e=0;e<ann.size();e++) 
    {
     
      //
      // skip this annotation?
      //
      
      if ( globals::specified_annots.size() > 0 && 
	   globals::specified_annots.find( ann[e] ) == globals::specified_annots.end() ) 
	continue;
      
      //
      // ignore this annotation if past the end?
      //
      
      if ( e >= ne ) continue;
      
      //
      // otherwise, create the new annotation class
      //
      
      annot_t * a = parent_edf.timeline.annotations.add( ann[e] );

      amap[ ann[e] ] = a;
      
      a->description = ann[e];
      
      a->file = filename ;
      
      a->type = globals::A_FLAG_T;  // no meta-data from .eannot 
      
      a->types.clear();
  
    }
  
  
  //
  // Populate intervals
  //

  if ( unepoched ) 
    {

      for ( int e = 0 ; e < ann.size() ; e++ )
	{
	  
	  if ( amap.find( ann[e] ) != amap.end() )
	    {
	      
	      interval_t interval( e * elen , e * elen + einc );
	      
	      annot_t * a = amap[ ann[e] ];
	      
	      instance_t * instance = a->add( ann[e] , interval );
	      
	      // track how many annotations we add
	      parent_edf.aoccur[ a->name ]++;
	      
	    }
	  
	} // next epoch 
      
    }
  else
    {

      // but if we do already have an in-memory EDF, which might be
      // discontinuous, we need to use the timeline to get the 
      // proper interval for the e'th epoch
      
      parent_edf.timeline.first_epoch();
      
      std::vector<int> epoch_counts;

      int e = 0;

      while ( 1 ) 
	{

	  int epoch = parent_edf.timeline.next_epoch_ignoring_mask();
	  
	  if ( epoch == -1 ) break;
          
	  if ( e >= ann.size() ) Helper::halt( "internal error map_epoch_annot()" );

	  interval_t interval = parent_edf.timeline.epoch( epoch );
	  
	  annot_t * a = amap[ ann[e] ];
	  
	  instance_t * instance = a->add( ann[e] , interval );
	  
	  // track how many annotations we add
	  parent_edf.aoccur[ a->name ]++;
	  
	  // next row in file
	  ++e;

	}

    }

  
  //
  // all done
  //
  
  return true;
}


bool annot_t::special() const
{
  if ( name == "duration_hms" ) return true;
  if ( name == "duration_sec" ) return true;
  if ( name == "epoch_sec" ) return true; 
  if ( name == "start_hms" ) return true; 
  return false;
}


bool annot_t::process_special( const std::string & , const std::string & )
{
  // look for special flags, and add to 
  return true;
}

bool annot_t::load( const std::string & f , edf_t & parent_edf )
{

  //
  // static annot_t function, which will create multiple annot_t for each new 'name'
  // encountered
  //
  
  // 
  // Check file exists and is of the correct type
  //

  if ( ! Helper::fileExists(f) ) return -1;

  if ( Helper::file_extension( f , "xml" ) ) 
    {
      Helper::halt( f + " is an XML file... should already have been loaded (internal error)" );
      return -1;
    }
  
  if ( Helper::file_extension( f , "ftr" ) ) 
    {
      Helper::halt( f + " is an FTR file... should already have been loaded (internal error)" );
      return -1;
    }
  

  //
  // A simple epoch annotation file? (based on extension or NOT having # as first char
  // i.e. no header).  These are not allowed for EDF+ files.
  //

  bool is_eannot = Helper::file_extension( f , "eannot" ) ;

  if ( is_eannot && ( parent_edf.header.edfplus || ! parent_edf.header.continuous ) ) 
    Helper::halt( "cannot use .eannot files with (discontinuous) EDF+ files" );
       
  if ( ! is_eannot )
    {
      std::ifstream IN1( f.c_str() , std::ios::in );
      std::string x;
      Helper::safe_getline( IN1 , x );
      if ( IN1.eof() ) return false;
      IN1.close();
      if ( x == "" ) return false;
      if ( x[0] != '#' ) is_eannot = true;
    }
  
  if ( is_eannot )
    {
      std::vector<std::string> a;
      
      std::ifstream IN1( f.c_str() , std::ios::in );
      while ( ! IN1.eof() )
	{
	  std::string x;
	  Helper::safe_getline( IN1 , x );
	  if ( IN1.eof() ) break;
	  if ( x == "" ) continue;

	  // remap? (and if so, track)
	  std::string y = nsrr_t::remap( x ) ;
	  if ( y != x ) parent_edf.timeline.annotations.aliasing[ y ] = x ;

	  // store
	  a.push_back( y );
	}
      IN1.close();

      annot_t::map_epoch_annotations( parent_edf , 
				      a , 
				      f , 
				      parent_edf.timeline.epoch_len_tp() , 
				      parent_edf.timeline.epoch_increment_tp() );
      
      
      return true;
      
    }


  //
  // Otherwise, this is an .annot file   
  //

  
  std::ifstream FIN( f.c_str() , std::ios::in );
  
  // header with # character
  
  // types: int, str, dbl, bool
  // [txt] [str] 
  // [dbl] [num]
  // [int] 
  // [yn] [bool]
  // [.] none
  
  // # name1 | description | col1(int) col2(str) col3(dbl) 
  // # name2 | description | col1(str)
  // # name3 | description                              [ just means a bool ] 
  
  // then rows are either interval or epoch-based 
  
  // name  id1  sec1  sec2  { vars }
  // name  id   e:1   {e:2} { vars } 
  // name  id   hh:mm:ss  hh:mm:ss { vars }

  // assume e:1   means 30-second epoch, no overlap  [ hard-code this ] 
  // e:1:20 is first of a 20-second epoch
  // e:1:20:10 is similar, but w/ epoch overlap of 10 seconds (10=increment)

  // check EDF starttime, which might be needed
  
  clocktime_t starttime( parent_edf.header.starttime );
    
  // read header then data
  
  bool epoched = false;
  
  int line_count = 0;

  std::map<std::string,annot_t*> annot_map;
  
  std::map<annot_t*,std::vector<std::string> > cols;
    
  while ( ! FIN.eof() )
    {
      
      if ( FIN.bad() ) continue;
      std::string line;      
      Helper::safe_getline( FIN , line );      
      if ( FIN.eof() || line == "" ) continue;
      
      //
      // header or data row?
      //
   
      if ( line[0] == '#' ) 
	{
	  
	  // skip initial # here
	  std::vector<std::string> tok = Helper::parse( line.substr(1) , "|" );
	  
	  if ( tok.size() < 1 || tok.size() > 3 ) Helper::halt( "bad header for format\n" + line );

	  //
	  // Get the name and ID 
	  //

	  std::string name = Helper::trim( tok[0] );
	  
	  //
	  // skip this annotation
	  //

	  if ( globals::specified_annots.size() > 0 && 
	       globals::specified_annots.find( name ) == globals::specified_annots.end() ) 
	    continue;
	    
	  
	  //
	  // otherwise, create the annotation if it doesn't exist
	  //

	  annot_t * a = parent_edf.timeline.annotations.add( name );


	  //
	  // store a temporary lookup table
	  //
	  annot_map[ name ] = a;
	  
	  //
	  // Other details
	  //
	  
	  a->description = tok.size() >= 2 ? Helper::trim( tok[1] ) : name ; 
	  
	  a->file = f;
	  
	  a->type = globals::A_FLAG_T; // unless we learn otherwise, i.e. just below when parsing header line

	  a->types.clear();
	  
	  // columns specified
	  if ( tok.size() == 3 ) 
	    {
	      std::vector<std::string> type_tok = Helper::parse( tok[2] , " \t" );
	      
	      for (int j=0;j<type_tok.size();j++)
		{
		  std::vector<std::string> type_tok2 = Helper::parse( type_tok[j] , "[(" );
		  if ( type_tok2.size() > 2 ) Helper::halt( "bad type '" + type_tok[j] + "'" );
		  
		  // column name
		  const std::string var_name = type_tok2[0] ;
		  
		  // track order of columns for this annotation 
		  cols[a].push_back( var_name );

		  // type		  
		  globals::atype_t t = globals::A_NULL_T;
		  
		  if ( type_tok2.size() == 1 ) 
		    t = globals::A_FLAG_T;
		  else
		    {
		      char c = type_tok2[1][ type_tok2[1].size() - 1 ] ;
		      if ( c != ']' && c != ')' ) Helper::halt( "bad type '" + type_tok[j] + "' -> " + c );
		      std::string tstr  = type_tok2[1].substr( 0 , type_tok2[1].size() - 1 );
		      
		      if ( globals::name_type.find( tstr ) != globals::name_type.end() )
			t = globals::name_type[ tstr ];

		    }
		  
		  if ( t == globals::A_NULL_T )
		    Helper::halt( "unsupported annotation type from\n" + line );

		  a->types[ var_name ] = t ; 
		
		  // if only a single TYPE has been specified, assign this to the 
		  // annotation class, otherwise set it as undefined
		  
		  if ( type_tok.size() == 1 ) 
		    a->type = t ; 
		  else // i.e. instead of 'FLAG', this means that we have multiple types
		    a->type = globals::A_NULL_T ; 
		  
		} // next column for this annotation
	      
	    }
	  
	}


      //
      // otherwise, assume this is a data row
      //

      else 
	{

	  // data-rows are tab-delimited
	  
	  std::vector<std::string> tok = Helper::parse( line , " \t" );
	  
	  if ( tok.size() == 0 ) continue; 

	  // are we skipping this annotation anyway?

	  if ( globals::specified_annots.size() > 0 && 
	       globals::specified_annots.find( tok[0] ) == globals::specified_annots.end() ) 
	    continue;
	  
	  // was this annotation specified in the header? 
	  
	  std::map<std::string,annot_t*>::iterator aa = annot_map.find( tok[0] );
	  
	  if ( aa == annot_map.end() ) 
	    Helper::halt( "annotation " + tok[0] + " not in header of " + f );
	  
	  annot_t * a = aa->second; 			
	  
	  std::string id = tok[1];

	  // epoch (single or range) or an interval (range)? 
	  
	  if ( tok.size() < 3 ) Helper::halt( "bad line format, need at least 3 cols:\n" + line );
	  
	  bool eline = tok[2][0] == 'e' ;
	  
	  bool erange = tok.size() >= 4 && tok[3][0] == 'e'; 
	  
	  bool esingle = eline && ! erange;
	  
	  // expected # of cols

	  const int expected = 2 + ( esingle ? 1 : 2 ) + a->types.size() ; 
	  
	  if ( tok.size() != expected ) 
	    Helper::halt( "bad line format: saw " + Helper::int2str( (int)tok.size() ) + " cols but expecting " 
			  + Helper::int2str( expected) + " for " + a->name + "\n" + line );
	  
	  
	  interval_t interval;
	  
	  if ( eline )
	    {

	      if ( parent_edf.header.edfplus || ! parent_edf.header.continuous ) 
		Helper::halt( "cannot use e:1 notation in .annot files with (discontinuous) EDF+ files" );
	      
	      
	      // 2 e:1        assumes 30
	      // 3 e:30:1     assumes no overlap
	      // 4 e:15:5:4   specifies all
	      
	      std::vector<std::string> tok2 = Helper::parse( tok[2] , ":" );
	      
	      if ( tok2.size() < 2 || tok2.size() > 4 ) 
		Helper::halt( "bad epoch specification, expecting e:1, e:30:1, e:30:30:1, etc" );
	      
	      if ( tok2[0] != "e" ) 
		Helper::halt( "bad epoch specification, expecting e:1, e:30:1, e:30:30:1, etc" );	    
	      
	      int epoch_length = globals::default_epoch_len;
	      int epoch_increment = globals::default_epoch_len; // i.e. non-overlapping
	      int epoch;
	      
	      if ( ! Helper::str2int( tok2[ tok2.size() - 1 ] , &epoch ) ) 
		Helper::halt( "invalid epoch: " + tok[2] );
	      
	      if ( epoch == 0 ) 
		Helper::halt( "invalid E value of '0' (first epoch should be '1')" );

	      if ( tok2.size() >= 3 ) 
		if ( ! Helper::str2int( tok2[ 1 ] , &epoch_length ) ) 
		  Helper::halt( "invalid epoch length:  " + tok[2] );

	      if ( tok2.size() == 4  ) 
		if ( ! Helper::str2int( tok2[ 1 ] , &epoch_increment ) ) 
		  Helper::halt( "invalid epoch increment:  " + tok[2] );
	      
	      uint64_t epoch_length_tp = Helper::sec2tp( epoch_length );
	      
	      uint64_t epoch_increment_tp = Helper::sec2tp( epoch_increment );
	      
	      // set interval from current line
	      // last point is defined as point *past* end of interval
	      
	      interval.start = epoch_increment_tp * (epoch-1);
	      interval.stop  = interval.start + epoch_length_tp;
	      
	      //
	      // A second epoch ?   in this case, overwrite interval.stop from above
	      //
	      
	      if ( erange ) 
		{
		    

		  std::vector<std::string> tok2 = Helper::parse( tok[3] , ":" );
		  
		  if ( tok2.size() < 2 || tok2.size() > 4 ) 
		    Helper::halt( "bad epoch specification, expecting e:1, e:30:1, e:30:30:1, etc" );
		  
		  if ( tok2[0] != "e" ) 
		    Helper::halt( "bad epoch specification, expecting e:1, e:30:1, e:30:30:1, etc" );	    
		  
		  int epoch;
		  
		  if ( ! Helper::str2int( tok2[ tok2.size() - 1 ] , &epoch ) ) 
		    Helper::halt( "invalid epoch: " + tok[2] );
		  
		  if ( epoch == 0 ) 
		    Helper::halt( "invalid E value of '0' (first epoch should be '1')" );
		  
		  if ( tok2.size() >= 3 ) 
		    if ( ! Helper::str2int( tok2[ 1 ] , &epoch_length ) ) 
		      Helper::halt( "invalid epoch length:  " + tok[2] );
		  
		  if ( tok2.size() == 4  ) 
		    if ( ! Helper::str2int( tok2[ 1 ] , &epoch_increment ) ) 
		      Helper::halt( "invalid epoch increment:  " + tok[2] );
		  
		  uint64_t epoch_length_tp = Helper::sec2tp( epoch_length );
		  
		  uint64_t epoch_increment_tp = Helper::sec2tp( epoch_increment );
		  
		  
		  // set interval from current line
		  // last point is defined as point *past* end of interval
		  
		  uint64_t start_of_last_epoch = epoch_increment_tp * (epoch-1);
		  interval.stop  = start_of_last_epoch + epoch_length_tp;
		  
		}	      
	      
	    }
	  else // an INTERVAL
	    {
	      
	      // assume this is either a single numeric value (in seconds) which is an offset past the EDF start
	      // OR in clock-time, in hh:mm:ss (24-hour) format
	      
	      std::vector<std::string> tok_start_hms = Helper::parse( tok[2] , ":" );
	      std::vector<std::string> tok_stop_hms = Helper::parse( tok[3] , ":" );
	      
	      bool is_hms = tok_start_hms.size() == 3 && tok_stop_hms.size() == 3;
	      
	      if ( is_hms && ! starttime.valid ) 
		Helper::halt( "specifying hh:mm:ss clocktime annotations, but no valid starttime in the EDF" );
	      
	      // read as seconds 
	      double dbl_start = 0 , dbl_stop = 0;
	      
	      if ( is_hms )
		{
		  
		  clocktime_t atime( tok[2] );

		  clocktime_t btime( tok[3] );
		  
		  dbl_start = clocktime_t::difference( starttime , atime ) * 3600; 
		  
		  dbl_stop = clocktime_t::difference( starttime , btime ) * 3600; 
		  
		}
	      else
		{
		  
		  if ( ! Helper::str2dbl( tok[2] , &dbl_start ) )
		    Helper::halt( "invalid interval: " + line );
		  
		  if ( ! Helper::str2dbl( tok[3] , &dbl_stop ) ) 
		    Helper::halt( "invalid interval: " + line );
		  
		}
	      

	      if ( dbl_start < 0 ) Helper::halt( f + " contains row(s) with negative time points" ) ;

	      if ( dbl_stop < 0 ) Helper::halt( f + " contains row(s) with negative time points" ) ;

	      // convert to uint64_t time-point units

	      interval.start = Helper::sec2tp( dbl_start );
	      
	      // assume stop is already specified as 1 past the end, e.g. 30 60
	      // *unless* it is a single point, e.g. 5 5 
	      // which is handled below
	      
	      interval.stop  = Helper::sec2tp( dbl_stop );
	      
	    }
	  


	  if ( interval.stop == interval.start ) ++interval.stop;
	  
	  // the final point is interpreted as one past the end
	  // i.e. 0 30   means up to 30
	  // but a special case: for a single point specified,  1.00 1.00 
	  // rather than have this as illegal, assume the user meant a 
	  // 1-time-unit point; so advance the STOP by 1 unit
	  
	  if ( interval.start > interval.stop )
	    Helper::halt( "invalid interval: " + line );
	  
	  
	  //
	  // for a FLAG for this annotation (with same name as primary annotaton)
	  //
	  
	  instance_t * instance = a->add( id , interval );
	  
	  
	  // track how many annotations we add
	  parent_edf.aoccur[ a->name ]++;
	  
	  
	  const int n = tok.size();
	          

	  //
	  // Also add any other columns (as separate events under this same annotation)
	  //
	  
	  for (int j = (esingle ? 3 : 4 ) ;j<n; j++)
	    {

	      const int idx = j - (esingle ? 3 : 4 );
	      
	      const std::string & label = cols[a][idx];	      

	      globals::atype_t t = a->types[label];
	      
	      if ( t == globals::A_FLAG_T ) 
		{
		  instance->set( label );
		}
	      
	      else if ( t == globals::A_MASK_T )
		{
		  if ( tok[j] != "." )
		    {
		      // accepts F and T as well as long forms (false, true)
		      bool value = Helper::yesno( tok[j] );
		      instance->set_mask( label , value );
		    }
		}
	      
	      else if ( t == globals::A_BOOL_T )
		{
		  if ( tok[j] != "." )
		    {
		      // accepts F and T as well as long forms (false, true)
		      bool value = Helper::yesno( tok[j] );
		      instance->set( label , value );
		    }
		}

	      else if ( t == globals::A_INT_T )
		{
		  int value = 0;
		  if ( ! Helper::str2int( tok[j] , &value ) )
		    Helper::halt( "invalid E line, bad numeric value" );
		  instance->set( label , value );
		}

	      else if ( t == globals::A_DBL_T )
		{
		  double value = 0;
		  
		  if ( Helper::str2dbl( tok[j] , &value ) )		    
		    instance->set( label , value );
		  else
		    if ( tok[j] != "." && tok[j] != "NA" ) 
		      Helper::halt( "invalid E line, bad numeric value" );		  
		}

	      else if ( t == globals::A_TXT_T )
		{
		  instance->set( label , tok[j] );
		}
	      
	      //
	      // TODO.. add vector readers
	      //


	      else
		logger << "could not read undefined type from annotation file for " << label << "\n";

	      
// 	      // track how many annotations we add
// 	      parent_edf.aoccur[ a->name + ":" + label ]++;
	      
	    }
	  
	  ++line_count;
	}
      
    } // next line

  //  logger << "  processed " << line_count << " lines\n";
  
  FIN.close();
  
  return line_count;
}


int annot_t::load_features( const std::string & f )
{
  
  // set basic values for this annotation type, then add events/features  

  //logger << " attaching feature-list file " << f << "\n";
  
  std::ifstream FIN( f.c_str() , std::ios::in );
  
  int line_count = 0;
  
  // tab-delimited

  // tp1 tp2 label key=value key=value
  // with special values  _rgb=255,255,255
  //                      _value={float}

  while ( ! FIN.eof() )
    {
      
      if ( FIN.bad() ) continue;
      std::string line;      
      Helper::safe_getline( FIN , line );      
      if ( FIN.eof() || line == "" ) continue;
      
      std::vector<std::string> tok = Helper::parse( line , "\t" );
      const int n = tok.size();
      if ( n < 3 ) continue;
      
      feature_t feature;
      
      // features work directly in interval-TP coding, so no need to change the end-point
      if ( ! Helper::str2int64( tok[0] , &feature.feature.start ) ) Helper::halt( "bad format " + line + "\n" );
      if ( ! Helper::str2int64( tok[1] , &feature.feature.stop  ) ) Helper::halt( "bad format " + line + "\n" );
      feature.label = tok[2];

      if ( feature.feature.start > feature.feature.stop ) Helper::halt( "bad format, start > stop : " + line + "\n" );
      
      for (int t=3;t<tok.size();t++)
	{
	  std::vector<std::string> tok2 = Helper::parse( tok[t] , "=" );
	  if ( tok2.size() == 1 ) feature.data[ tok2[0] ] = "";
	  else 
	    {
	      
	      feature.data[ tok2[0] ] = tok2[1];
	      
	      if ( tok2[0] == "_rgb" ) 
		{
		  feature.has_colour = true;
		  feature.colour = tok2[1];
		}
	      else if ( tok2[0] == "_val" ) 
		{
		  feature.has_value = Helper::str2dbl( tok2[1] , &feature.value ) ;
		}
		
	    }
	}

      //
      // Add this interval
      //
	  
      instance_t * instance = add( feature.label , feature.feature );
      
      //
      // and append meta-data
      //
      
      instance->add( feature.data );
      
      //
      // and add the type information in too  (even though not every instance may have all types)
      //
      
      std::map<std::string,std::string>::const_iterator ss = feature.data.begin();
      while ( ss != feature.data.end() ) 
	{
	  types[ ss->first ] = globals::A_TXT_T;
	  ++ss;
	}
      
      //
      // row count
      //
      
      ++line_count;

    } // next line
  
  
  //  logger << "  processed " << line_count << " lines\n";
  
  FIN.close();
  
  return line_count;

}


bool annot_t::save( const std::string & t)
{

  std::ofstream FOUT( t.c_str() , std::ios::out );

  FOUT << "# "
       << name << " | "
       << description ;

  std::map<std::string,globals::atype_t>::const_iterator aa = types.begin();
  while ( aa != types.end() )
    {
      if ( aa == types.begin() ) FOUT << " |";
      FOUT << " " << aa->first << "[" << globals::type_name[ aa->second ] << "]";
      ++aa;
    }
  
  FOUT << std::fixed << std::setprecision(4);
  
  //
  // Interval-based annotation
  //
    
  annot_map_t::const_iterator ii = interval_events.begin();
  while ( ii != interval_events.end() )
    {
      
      const instance_idx_t & instance_idx = ii->first;
      const instance_t * instance = ii->second;
      
      FOUT << name << "\t"
	   << instance_idx.id << "\t"
	   << instance_idx.interval.start/(double)globals::tp_1sec << "\t" 
	   << (instance_idx.interval.stop-1LLU)/(double)globals::tp_1sec;  // note.. taking off the +1 end point
      
      std::map<std::string,avar_t*>::const_iterator ti = instance->data.begin();
      while ( ti != instance->data.end() )
	{
	  FOUT << "\t" << ti->second->text_value();
	  ++ti;
	}

      FOUT << "\n";
      ++ii;
    }
  
  FOUT.close();
  return true;
}  


void annot_t::dumpxml( const std::string & filename , bool basic_dumper )
{

  std::map<interval_t,std::vector<std::string> > res;
  
  XML xml( filename );
  if ( ! xml.valid() ) Helper::halt( "invalid annotation file: " + filename );
  
  if ( basic_dumper )
    {
      xml.dump();
      return;
    }
  
  // automatically determine format
  
  std::vector<element_t*> nsrr_format_test = xml.children( "PSGAnnotation" );
  bool profusion_format = nsrr_format_test.size() == 0 ;
  
  const std::string EventConcept = profusion_format ? "Name"           : "EventConcept" ;
  const std::string epoch_parent = profusion_format ? "CMPStudyConfig" : "PSGAnnotation" ;
  
  //
  // Epoch Length
  //

  int epoch_sec = -1;

  // Document --> CMPStudyConfig --> EpochLength 
  // PSGAnnotation --> EpochLength
    
  std::vector<element_t*> elements = xml.children( epoch_parent );
  for (int e=0;e<elements.size();e++)
    {
      if ( elements[e]->name == "EpochLength" ) 
	{
	  if ( ! Helper::str2int(  elements[e]->value , &epoch_sec ) ) 
	    Helper::halt( "bad EpochLength" ) ;
	  std::stringstream ss ;
	  ss << ".\t.\tEpochLength\t" << epoch_sec << "\n";
	  res[ interval_t(0,0) ].push_back( ss.str() );
	  break;
	}
    }
  
  if ( epoch_sec == -1 ) 
    {
      Helper::warn( "did not find EpochLength in XML, defaulting to " 
		    + Helper::int2str( globals::default_epoch_len ) + " seconds" );
      epoch_sec = globals::default_epoch_len;
    }


  //
  // Scored Events
  //

  std::vector<element_t*> scored = xml.children( "ScoredEvents" );
  
  // assume all annotations will then be under 'ScoredEvent'
  // Profusion: with children: 'EventConcept' , 'Duration' , 'Start' , and optionally 'Notes'
  // NSRR     : with children: 'Name'         , 'Duration' , 'Start' , and optionally 'Notes'
  
  for (int i=0;i<scored.size();i++)
    {

      element_t * e = scored[i];
      
      if ( ! Helper::iequals( e->name , "ScoredEvent" ) ) continue;
      
      element_t * concept  = (*e)( EventConcept );
      if ( concept == NULL ) concept = (*e)( "name" );
      
      element_t * start    = (*e)( "Start" );
      if ( start == NULL ) start = (*e)( "time" );

      element_t * duration = (*e)("Duration" );

      element_t * notes    = (*e)("Notes" );
     
      element_t * type    = (*e)( "EventType" );
      

      //      if ( concept == NULL || start == NULL || duration == NULL ) continue;
      if ( concept == NULL ) continue;

      double start_sec = 0, stop_sec = 0 , duration_sec = 0;
      uint64_t start_tp = 0 , stop_tp = 0;

      if ( duration != NULL )
	{
	  if ( ! Helper::str2dbl( duration->value , &duration_sec ) ) 
	    Helper::halt( "bad value in annotation" );	  		  
	}
      
      if ( start != NULL ) 
	{
	  if ( ! Helper::str2dbl( start->value , &start_sec ) ) 
	    Helper::halt( "bad value in annotation" );
	  stop_sec = start_sec + duration_sec;
	  start_tp = Helper::sec2tp( start_sec );
	  stop_tp = start_tp + Helper::sec2tp( duration_sec ) ; 
	  
	  // EDIT OUT	  
// 	  // in case duration was 0, make this a 1-time-unit event
// 	  if ( start_tp == stop_tp ) ++stop_tp;
	  
	  // MAKE ALL points one past the end
	  ++stop_tp;

	  //stop_tp = start_tp + (uint64_t)( globals::tp_1sec * duration_sec ) - 1LLU ; 
	  
	}

      interval_t interval( start_tp , stop_tp );
      
      std::stringstream ss;      

      if ( start != NULL ) 
	{
	  ss << start_sec ;
	  if ( duration != NULL ) ss << " - " << stop_sec << "\t"
				     << "(" << duration_sec << " secs)\t";
	  else ss << ".\t";
	}
      else ss << ".\t.\t";
      
      if ( type != NULL ) 
	ss << type->value << "\t";
      else 
	ss << ".\t";
      ss << concept->value << "\t";
      if ( notes != NULL ) ss << "\t" << notes->value ;      
      ss << "\n";
      res[ interval ].push_back( ss.str() );

    }
  

  //
  // Sleep Stages (Profusion only: in NSRR format, staging is incorporated as ScoredEvent)
  //
    
  // Profusion: under 'SleepStages', with children 'SleepStage' which comprises an integer value
  // 0  wake
  // 1  NREM1
  // 2  NREM2
  // 3  NREM3
  // 4  NREM4
  // 5  REM
  // Otherwise 'Unscored'
  

  if ( profusion_format )
    {

      std::vector<element_t*> scored = xml.children( "SleepStages" );
      
      int seconds = 0;

      for (int i=0;i<scored.size();i++)
	{

	  element_t * e = scored[i];

	  if ( e->name != "SleepStage" ) continue;
	  
	  std::string stg = "Unscored";
	  if      ( e->value == "0" ) stg = "wake";
	  else if ( e->value == "1" ) stg = "NREM1";
	  else if ( e->value == "2" ) stg = "NREM2";
	  else if ( e->value == "3" ) stg = "NREM3";
	  else if ( e->value == "4" ) stg = "NREM4";
	  else if ( e->value == "5" ) stg = "REM";	 
	 
	  interval_t interval( Helper::sec2tp( seconds ) , 
			       Helper::sec2tp( seconds + epoch_sec ) );

// 	  interval_t interval( (uint64_t)(seconds * globals::tp_1sec ) , 
// 			       (uint64_t)(( seconds + epoch_sec ) * globals::tp_1sec ) );

	  std::stringstream ss;      
	  ss << seconds << " - " << seconds + epoch_sec << "\t"
	     << "(" << epoch_sec << " secs)\t"
	     << "SleepStage" << "\t"	     
	     << stg << "\n";
	  res[ interval ].push_back( ss.str() );
	  
	  // advance to the next epoch
	  seconds += epoch_sec;
	  
	}
           
    }
  
  //
  // Report
  //
  
  std::map<interval_t,std::vector<std::string> >::const_iterator ii = res.begin();
  while ( ii != res.end() )
    {
      std::vector<std::string>::const_iterator jj = ii->second.begin();
      while ( jj != ii->second.end() )
	{
	  std::cout << *jj;
	  ++jj;
	}
      ++ii;
    }
 
}

bool annot_t::loadxml( const std::string & filename , edf_t * edf )
{

  //  logger << "  reading XML annotations from " << filename << "\n";
  
  XML xml( filename );

  if ( ! xml.valid() ) Helper::halt( "invalid annotation file: " + filename );

  //
  // Determine format: Profusion or NSRR or Luna ? 
  //
  
  std::vector<element_t*> nsrr_format_test = xml.children( "PSGAnnotation" );
  std::vector<element_t*> luna_format_test = xml.children( "Annotations" );
  
  bool profusion_format = nsrr_format_test.size() == 0 ;
  bool luna_format = luna_format_test.size() > 0 ;

  if ( globals::param.has( "profusion" ) ) profusion_format = true;
  
  if ( luna_format ) return loadxml_luna( filename , edf );

  const std::string EventConcept = profusion_format ? "Name"           : "EventConcept" ;
  const std::string epoch_parent = profusion_format ? "CMPStudyConfig" : "PSGAnnotation" ;
  
  
  std::vector<element_t*> scored = xml.children( "ScoredEvents" );
  

  //
  // NSRR format:
  //
  
  // assume all annotations will then be under 'ScoredEvent'
  // with children: 'EventConcept' , 'Duration' , 'Start' , and optionally 'Notes'
  
  //
  // Profusion format
  //
  
  // assume all annotations will then be under 'ScoredEvent'
  // with children: 'Name' , 'Duration' , 'Start' , and optionally 'Notes'
  
  // SleepStages: under separate 'SleepStages' parent
  // children elements 'SleepStage' == integer
  // ASSUME these are 30-s epochs, starting at 0
  

  //
  // First pass through all 'ScoredEvent's,, creating each annotation
  //

  std::set<std::string> added;
  
  for (int i=0;i<scored.size();i++)
    {
      
      element_t * e = scored[i];
      
      if ( ! Helper::iequals( e->name , "ScoredEvent" ) ) continue;
      
      element_t * concept  = (*e)( EventConcept );
      if ( concept == NULL ) concept = (*e)( "name" );
      
      if ( concept == NULL ) continue;
      
      // skip this..
      if ( concept->value == "Recording Start Time" ) continue;
      
      
      // annotation remap?
      std::string original_label = concept->value;
      concept->value = nsrr_t::remap( concept->value );
      
      // are we checking whether to add this file or no? 
      if ( globals::specified_annots.size() > 0 && 
	   globals::specified_annots.find( concept->value ) == globals::specified_annots.end() ) continue;
      
      // already found?
      if ( added.find( concept->value ) != added.end() ) continue;

      // otherwise, add

      if ( original_label != concept->value )
	  edf->timeline.annotations.aliasing[ concept->value ] = original_label ;

      annot_t * a = edf->timeline.annotations.add( concept->value );
      a->description = "XML-derived";
      a->file = filename;
      a->type = globals::A_FLAG_T; // not expecting any meta-data
      added.insert( concept->value );
    }

  //
  // Profusion-formatted sleep-stages?
  //
  
  if ( profusion_format )
    {
      
      std::vector<element_t*> scored = xml.children( "SleepStages" );
      
      for (int i=0;i<scored.size();i++)
	{
	  element_t * e = scored[i];

	  if ( e->name != "SleepStage" ) continue;
	  
	  std::string ss = "Unscored";
	  if      ( e->value == "0" ) ss = "wake";
	  else if ( e->value == "1" ) ss = "NREM1";
	  else if ( e->value == "2" ) ss = "NREM2";
	  else if ( e->value == "3" ) ss = "NREM3";
	  else if ( e->value == "4" ) ss = "NREM4";
	  else if ( e->value == "5" ) ss = "REM";	 
	  
	  // are we checking whether to add this file or no? 
	  
	  if ( globals::specified_annots.size() > 0 && 
	       globals::specified_annots.find( ss ) == globals::specified_annots.end() ) continue;
	  
	  // already found?
	  if ( added.find( ss ) != added.end() ) continue;
	  
	  // otherwise, add
	  annot_t * a = edf->timeline.annotations.add( ss );
	  a->description = "XML-derived";
	  a->file = filename;
	  a->type = globals::A_FLAG_T; // not expecting any meta-data from XML
	  added.insert( ss );
	  
	}
    }


  
  //
  // Back through, adding instances now we've added all annotations
  //

  for (int i=0;i<scored.size();i++)
    {
      
      element_t * e = scored[i];
      
      if ( ! Helper::iequals( e->name , "ScoredEvent" ) ) continue;
      
      element_t * concept  = (*e)( EventConcept );
      if ( concept == NULL ) concept = (*e)( "name" );

      // skip if we are not interested in this element
      if ( added.find( concept->value ) == added.end() ) continue;
      
      element_t * start    = (*e)( "Start" );
      if ( start == NULL ) start = (*e)( "time" );

      element_t * duration = (*e)( "Duration" );
      element_t * notes    = (*e)( "Notes" );

      if ( concept == NULL || start == NULL || duration == NULL ) continue;
      
      // otherwise, add 

      double start_sec, duration_sec;
      if ( ! Helper::str2dbl( start->value , &start_sec ) ) Helper::halt( "bad value in annotation" );
      if ( ! Helper::str2dbl( duration->value , &duration_sec ) ) Helper::halt( "bad value in annotation" );

      uint64_t start_tp = Helper::sec2tp( start_sec );

//       uint64_t stop_tp  = duration_sec > 0 
// 	? start_tp + (uint64_t)( duration_sec * globals::tp_1sec ) - 1LLU 
// 	: start_tp;

      // std::cout << "xxx " <<  concept->value << "\t" << start_sec << "\t" << duration_sec << "\t"
      // 		<< duration_sec * globals::tp_1sec  << "\t" <<  (uint64_t)( duration_sec ) * globals::tp_1sec  << "\n";
      
      // stop is defined as 1 unit past the end of the interval
      uint64_t stop_tp  = duration_sec > 0 
	? start_tp + Helper::sec2tp( duration_sec )
	: start_tp + 1LLU ;
      
      interval_t interval( start_tp , stop_tp );
      
      annot_t * a = edf->timeline.annotations.add( concept->value );
      
      if ( a == NULL ) Helper::halt( "internal error in loadxml()");

      instance_t * instance = a->add( concept->value , interval );      
      
      // any notes?  set as TXT, otherwise it will be listed as a FLAG
      if ( notes ) 
	{
	  instance->set( concept->value , notes->value );  
	}
      
    }
  
  
  //
  // Profusion-formatted sleep-stages?
  //
  
  if ( profusion_format )
    {
  
      std::vector<element_t*> scored = xml.children( "SleepStages" );
      
      int start_sec = 0;
      int epoch_sec = 30;

      // assume 30-second epochs, starting from 0...

      for (int i=0;i<scored.size();i++)
	{
	  element_t * e = scored[i];

	  if ( e->name != "SleepStage" ) continue;
	  
	  std::string ss = "Unscored";
	  if      ( e->value == "0" ) ss = "wake";
	  else if ( e->value == "1" ) ss = "NREM1";
	  else if ( e->value == "2" ) ss = "NREM2";
	  else if ( e->value == "3" ) ss = "NREM3";
	  else if ( e->value == "4" ) ss = "NREM4";
	  else if ( e->value == "5" ) ss = "REM";	 


	  // skip if we are not interested in this element
	  
	  if ( added.find( ss ) == added.end() ) continue;
	  
	  // otherwise, add
	  
	  uint64_t start_tp = Helper::sec2tp( start_sec );
	  uint64_t stop_tp  = start_tp + Helper::sec2tp( epoch_sec ) ; // 1-past-end encoding
	  
	  // advance to the next epoch
	  start_sec += epoch_sec;
	  
	  interval_t interval( start_tp , stop_tp );	  
	  
	  annot_t * a = edf->timeline.annotations.add( ss );
	  
	  instance_t * instance = a->add( ss , interval );      
      
	  instance->set( ss );
	  
	}
            
    }



  //
  // Misc. test code: have signal descriptions in XMLs
  //

  if ( false )
    {
      std::vector<element_t*> signals = xml.children( "Signals" );

      //std::cout << "signals size = " << signals.size() << "\n";
      
      for (int i=0; i<signals.size(); i++)
	{
	  
	  element_t * e = signals[i];
	  
	  //       cmd_t::signal_alias( str )
	  // 	"canonical|alias1|alias2"
	  
	  element_t * label = (*e)("Label");
	  element_t * canonical_label = (*e)("CanonicalLabel");
	  element_t * desc = (*e)("Description");
	  
	  if ( label ) std::cout << "label = " << label->value << "\n";
	  if ( canonical_label ) std::cout << "canon " << canonical_label->value << "\n";
	  if ( desc ) std::cout << "Desc " << desc->value << "\n";
	  
	  if ( label->value != "" && 
	       canonical_label->value != "" && 
	       label->value != canonical_label->value )
	    {
	      logger << "  changing " << label->value << " to canonical label " << canonical_label->value << "\n";
	      edf->header.rename_channel( label->value , canonical_label->value );
	      cmd_t::signal_alias( canonical_label->value + "|" + label->value ); // include this????
	    }
	  
	  std::vector<element_t*> attr = e->children( "Attributes" );
	  
	  for (int j=0;j<attr.size();j++)
	    {

	      element_t * ee = attr[j];
	      
	      if ( ee->name != "Attribute" ) continue;
	      
	      element_t * aname = (*ee)( "AttributeKey" );
	      element_t * aval  = (*ee)( "AttributeValue" );
	      if ( aname != NULL && aval != NULL ) 
		std::cout << aname->value << " = " << aval->value << "\n";
	      
	    }
	
	  // Signal
	  //  -Label
	  //  -CanonicalLabel
	  //  -Description
	  //  -Attributes
	  //    Attribute
	  //     -AttrinuteKey 
	  //     -AttributeLabel      
	  
	}
    }



  return true;
}






bool annot_t::savexml( const std::string & f )
{
  Helper::halt( "not yet implemented" );
  return true;
}




annot_map_t annot_t::extract( const interval_t & window ) 
{
  
  //
  // Fetch all annotations that overlap this window
  // where overlap is defined as region A to B-1 for interval_t(A,B)
  //

  annot_map_t r; 
  
  // urghhh... need to implement a much better search... 
  // but for now just use brute force... :-(
  
  annot_map_t::const_iterator ii = interval_events.begin();
  while ( ii != interval_events.end() )
    {
      const interval_t & a = ii->first.interval;
      if ( a.overlaps( window ) ) r[ ii->first ] = ii->second;
      else if ( a.is_after( window ) ) break;
      ++ii;
    }
  
  return r;

  
  // // find first interval just /before/ then step over deciding which
  // // should fit in

  // interval_evt_map_t::const_iterator ii 
  //   = interval_events.lower_bound( window );

  // interval_evt_map_t::const_iterator jj = ii; 
  
  // // if returns iterator to end(), could still overlap the 
  // // one or more events at the end, so need to check for this
  // // special case of looking backwards either way; 
  
  // // back
  // while ( 1 ) 
  //   {

  //     if ( jj == interval_events.begin() ) break;
  //     --jj;
  //     const interval_t & a = jj->first;
      
  //     //      std::cout << "extract: considering " << a.start << " - " << a.stop << "\n";
      
  //     if ( a.overlaps( window ) ) 
  // 	{
  // 	  r[ a ] = jj->second;
  // 	  //std::cout << " found overlap\n";
  // 	}
  //     else if ( a.is_before( window ) )
  // 	{
  // 	  // also need to consider intervals that start before 'a' but still span window
  // 	  // i.e. go back until test interval doesn't overlap 'a'
  // 	  interval_evt_map_t::const_iterator kk = jj; 
  // 	  interval_t prev = a;
  // 	  while ( 1 ) 
  // 	    {
  // 	      if ( kk == interval_events.begin() ) break;
  // 	      --kk;
  // 	      interval_t b = kk->first;
  // 	      if ( ! prev.overlaps( b ) ) break; // really all done now
  // 	      if ( b.overlaps( window ) ) 
  // 		{ 
  // 		  r[ b ] = kk->second; 
  // 		  std::cout << " XXX added an extra!\n"; 		  
  // 		}
  // 	      prev = b;
  // 	    }
  // 	  break;      
  // 	}
  //   }

  // std::cout << "now forward...\n";
  // // forward
  // while ( 1 ) 
  //   {      
  //     if ( ii == interval_events.end() ) break;
  //     const interval_t & a = ii->first;
  //     std::cout << "extract: considering " << a.start << " - " << a.stop << "\n";
  //     if ( a.overlaps( window ) ) 
  // 	{
  // 	  r[ a ] = ii->second;
  // 	  std::cout << " found overlap\n";
  // 	}
      
  //     else if ( a.is_after( window ) ) break;      
  //     ++ii;
  //   }
  // std::cout << "  done...\n";
  // return r;
  
}


bool annotation_set_t::make_sleep_stage( const std::string & a_wake , 
					 const std::string & a_n1 , 
					 const std::string & a_n2 , 
					 const std::string & a_n3 , 
					 const std::string & a_n4 , 
					 const std::string & a_rem ,
					 const std::string & a_other )
{

  //
  // already made?
  //
  
  if ( find( "SleepStage" ) != NULL ) return false; 


  //
  // Use default annotation labels, if not otherwise specified
  // 

  std::string dwake, dn1, dn2, dn3, dn4, drem, dother;
  
  std::map<std::string,annot_t*>::const_iterator ii = annots.begin();
  while ( ii != annots.end() )
    {
      const std::string & s = ii->first;
      
      sleep_stage_t ss = globals::stage( s );
      
      if      ( ss == WAKE )     dwake = s;
      else if ( ss == NREM1 )    dn1 = s;
      else if ( ss == NREM2 )    dn2 = s;
      else if ( ss == NREM3 )    dn3 = s;
      else if ( ss == NREM4 )    dn4 = s;
      else if ( ss == REM )      drem = s;
      else if ( ss == UNSCORED ) dother = s;
      else if ( ss == MOVEMENT ) dother = s;
      else if ( ss == ARTIFACT ) dother = s;
      // if ss == UNKNOWN means this is not a Sleep Stage
      ++ii;
    }


  std::vector<std::string> v_wake  = Helper::parse( a_wake , "," );
  std::vector<std::string> v_n1    = Helper::parse( a_n1 , "," );
  std::vector<std::string> v_n2    = Helper::parse( a_n2 , "," );
  std::vector<std::string> v_n3    = Helper::parse( a_n3 , "," );
  std::vector<std::string> v_n4    = Helper::parse( a_n4 , "," );
  std::vector<std::string> v_rem   = Helper::parse( a_rem , "," );
  std::vector<std::string> v_other = Helper::parse( a_other , "," );

  // add defaults
  if ( v_wake.size() == 0 ) v_wake.push_back( dwake );
  if ( v_n1.size() == 0 ) v_n1.push_back( dn1 );
  if ( v_n2.size() == 0 ) v_n2.push_back( dn2 );
  if ( v_n3.size() == 0 ) v_n3.push_back( dn3 );
  if ( v_n4.size() == 0 ) v_n4.push_back( dn4 );
  if ( v_rem.size() == 0 ) v_rem.push_back( drem );
  if ( v_other.size() == 0 ) v_other.push_back( dother );
  

  //
  // find annotations, allowing a comma-delimited list
  //

  std::vector<annot_t *> wakes, n1s, n2s, n3s, n4s, rems, others;
  
  for (int a=0;a<v_wake.size();a++)
    wakes.push_back( find( v_wake[a] ) );
  
  for (int a=0;a<v_n1.size();a++)
    n1s.push_back( find( v_n1[a] ) );
  
  for (int a=0;a<v_n2.size();a++)
    n2s.push_back( find( v_n2[a] ) );

  for (int a=0;a<v_n3.size();a++)
    n3s.push_back( find( v_n3[a] ) );

  for (int a=0;a<v_n4.size();a++)
    n4s.push_back( find( v_n4[a] ) );

  for (int a=0;a<v_rem.size();a++)
    rems.push_back( find( v_rem[a] ) );
  
  for (int a=0;a<v_other.size();a++)
    others.push_back( find( v_other[a] ) );



  //
  // Check we had sensible annotations
  //

  int assigned = 0;

  for (int a=0;a<n1s.size();a++) if ( n1s[a] != NULL ) ++assigned;
  for (int a=0;a<n2s.size();a++) if ( n2s[a] != NULL ) ++assigned;
  for (int a=0;a<n3s.size();a++) if ( n3s[a] != NULL ) ++assigned;
  for (int a=0;a<rems.size();a++) if ( rems[a] != NULL ) ++assigned;
  for (int a=0;a<wakes.size();a++) if ( wakes[a] != NULL ) ++assigned;

  if ( assigned == 0 )
    return false;

  
  //
  // Create the 'SleepStage' unified annotation (used by HYPNO, STAGE, and SUDS)
  //
  
  annot_t * ss = add( "SleepStage" );

  ss->description = "SleepStage";

  for ( int i=0; i<wakes.size(); i++ )
    {
      annot_t * wake = wakes[i];
      if ( wake ) 
	{
	  annot_map_t & events = wake->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( WAKE ) , ee->first.interval );
	      ++ee;
	    }
	}
    }


  for ( int i=0; i<n1s.size(); i++ )
    {
      annot_t * n1 = n1s[i];
      if ( n1 ) 
	{
	  annot_map_t & events = n1->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( NREM1 ) , ee->first.interval );
	      ++ee;
	    }
	}
    }


  for ( int i=0; i<n2s.size(); i++ )
    {
      annot_t * n2 = n2s[i];      
      if ( n2 ) 
	{
	  annot_map_t & events = n2->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( NREM2 ) , ee->first.interval );
	      ++ee;
	    }
	}
    }

  
  for ( int i=0; i<n3s.size(); i++ )
    {
      annot_t * n3 = n3s[i];
      if ( n3 ) 
	{
	  annot_map_t & events = n3->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( NREM3 ) , ee->first.interval );
	      ++ee;
	    }
	}
    }


  for ( int i=0; i<n4s.size(); i++ )
    {
      annot_t * n4 = n4s[i];
      if ( n4 ) 
	{
	  annot_map_t & events = n4->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( NREM4 ) , ee->first.interval );
	      ++ee;
	    }
	}
    }


  for ( int i=0; i<rems.size(); i++ )
    {
      annot_t * rem = rems[i];
      if ( rem ) 
	{
	  annot_map_t & events = rem->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {
	      instance_t * instance = ss->add( globals::stage( REM ) , ee->first.interval );
	      ++ee;
	    }
	}
    }
  
  for ( int i=0; i<others.size(); i++ )
    {
      annot_t * other = others[i];
      if ( other ) 
	{
	  annot_map_t & events = other->interval_events;
	  annot_map_t::const_iterator ee = events.begin();
	  while ( ee != events.end() )
	    {	  
	      instance_t * instance = ss->add( globals::stage( UNSCORED ) , ee->first.interval );
	      ++ee;
	    }
	}
    }
  
  return true;
  
}


uint64_t annot_t::minimum_tp() const
{
  if ( interval_events.size() == 0 ) return 0;
  return interval_events.begin()->first.interval.start;
}


uint64_t annot_t::maximum_tp() const
{
  if ( interval_events.size() == 0 ) return 0;
  return (--interval_events.end())->first.interval.stop;
}


std::set<std::string> annot_t::instance_ids() const
{
  std::set<std::string> r;
  annot_map_t::const_iterator ii = interval_events.begin();
  while ( ii != interval_events.end() )
    {
      r.insert( ii->first.id );
      ++ii;
    }
  return r;
}



//
// Silly helper functions...
//

std::vector<bool> annot_t::as_bool_vec( const std::vector<int> & x ) { 
  std::vector<bool> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = (bool)x[i];
  return y;
} 

std::vector<bool> annot_t::as_bool_vec( const std::vector<double> & x ) { 
  std::vector<bool> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = (bool)x[i];
  return y;
} 

std::vector<bool> annot_t::as_bool_vec( const std::vector<std::string> & x ) { 
  std::vector<bool> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = Helper::yesno( x[i] );
  return y;
} 


// to int
std::vector<int> annot_t::as_int_vec( const std::vector<bool> & x ) { 
  std::vector<int> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = (int)x[i];
  return y;
} 

std::vector<int> annot_t::as_int_vec( const std::vector<double> & x ) { 
  std::vector<int> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = (int)round(x[i]);
  return y;
} 

std::vector<int> annot_t::as_int_vec( const std::vector<std::string> & x ) { 
  std::vector<int> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = (int)Helper::yesno( x[i] );
  return y;
} 

// to dbl
std::vector<double> annot_t::as_dbl_vec( const std::vector<bool> & x ) { 
  std::vector<double> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = x[i];
  return y;
} 

std::vector<double> annot_t::as_dbl_vec( const std::vector<int> & x ) { 
  std::vector<double> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = x[i];
  return y;
} 

std::vector<double> annot_t::as_dbl_vec( const std::vector<std::string> & x ) { 
  std::vector<double> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = Helper::yesno( x[i] );
  return y;
} 

// to txt
std::vector<std::string> annot_t::as_txt_vec( const std::vector<bool> & x ) { 
  std::vector<std::string> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = x[i] ? "true" : "false" ;
  return y;
} 

std::vector<std::string> annot_t::as_txt_vec( const std::vector<int> & x ) { 
  std::vector<std::string> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = x[i] == 0 ? "false" : "true" ;
  return y;
} 

std::vector<std::string> annot_t::as_txt_vec( const std::vector<double> & x ) { 
  std::vector<std::string> y( x.size() );
  for (int i=0;i<x.size();i++) y[i] = x[i] == 0 ? "false" : "true" ;
  return y;
} 



//
// Implement the EVAL command
//


void proc_eval( edf_t & edf , param_t & param )
{
  
  // expects a single parameter: 
  //   annot=name
  //   expr=# expression #
  //   globals=J,K,L
  
  
  std::string new_annot_class = param.requires( "annot" );
  
  std::string expression = Helper::unquote( param.requires( "expr" ) , '#' );
   
  std::set<std::string> acc_vars;
  
  bool use_globals = param.has( "globals" );
  if ( use_globals ) 
    acc_vars = param.strset( "globals" );
  
  logger << "  evaluating expression           : " << expression << "\n";
  logger << "  derived values annotation class : " << new_annot_class ;
  if ( use_globals ) logger << " (and " << new_annot_class << "_global)";
  logger << "\n";
  
  //
  // Get all existing annotations
  //
  
  std::vector<std::string> names = edf.timeline.annotations.names();


  //
  // Create/attach new annotation class, which will have multiple
  // epoch-level instances 
  //
  
  annot_t * new_annot = edf.timeline.annotations.add( new_annot_class );
  

  // 
  // Make global annotation an entirely separate class of annotation
  //
  
  annot_t * global_annot = use_globals ? edf.timeline.annotations.add( new_annot_class + "_global" ) : NULL ;
 
  instance_t dummy;
  
  instance_t * accumulator = use_globals ? global_annot->add( "." , edf.timeline.wholetrace() ) : &dummy ;
  
  //
  // We need to initialize any global variables that will appear in the main expression
  // Assume these are all floats for now, and will have the form _var 
  //
  
  if ( use_globals ) 
    {
      std::set<std::string>::const_iterator ii = acc_vars.begin();
      while ( ii != acc_vars.end() ) 
	{
	  accumulator->set( *ii , 0 );
	  ++ii;
	}
    }

  //
  // Iterate over epochs
  //

  edf.timeline.first_epoch();
  
  int acc_total = 0 , acc_retval = 0 , acc_valid = 0; 

  while ( 1 ) 
    {

      // consider _ALL_ epochs
      
      int e = edf.timeline.next_epoch_ignoring_mask() ;
      
      if ( e == -1 ) break;
      
      interval_t interval = edf.timeline.epoch( e );
	  
      std::map<std::string,annot_map_t> inputs;

      // get each annotations
      for (int a=0;a<names.size();a++)
	{
	  
	  annot_t * annot = edf.timeline.annotations.find( names[a] );
	  
	  // get overlapping annotations for this epoch
	  annot_map_t events = annot->extract( interval );
	  
	  // store
	  inputs[ names[a] ] = events;
	}
      
     
      //
      // create new annotation
      //
      
      instance_t * new_instance = new_annot->add( "e:" + Helper::int2str( edf.timeline.display_epoch(e) ) , interval );

      //
      // evaluate the expression
      //

      Eval tok( expression );
      
      tok.bind( inputs , new_instance , accumulator , &acc_vars );
      
      bool is_valid = tok.evaluate();
      
      bool retval;
      
      if ( ! tok.value( retval ) ) is_valid = false;
      
      //
      // Output
      //
  
      acc_total++;

      acc_valid += is_valid;

      if ( acc_valid ) 
	{
	  
	  acc_retval += retval;

	}
      
      // remove instance if expression was F or invalid
      if ( ( ! acc_valid ) || ( ! retval ) ) 
	{
	  new_annot->remove( "e:" + Helper::int2str( edf.timeline.display_epoch(e) ) , interval );
	}
            
      // next epoch
    } 
  

  //
  // show accumulator output in log
  //

  logger << "  evaluated expressions/epochs  " 
	 << acc_total << " ("
	 << acc_valid << " valid, " 
	 << acc_retval << " true)\n";
  logger << "  global variables (if any):\n" << accumulator->print( "\n" , "\t" ) ;
  
  logger << "\n";

  // all done 
  return;
  
}





void annotation_set_t::write( const std::string & filename , param_t & param )
{
  
  // write all annotations here as a single file; 
  // either XML or .annot file format
  // default is as XML  
  // order by time
  
  bool annot_format = param.has( "luna" );

  bool xml_format = param.has( "xml" ) || ! annot_format;
  
  if ( filename=="" ) Helper::halt( "bad filename for WRITE-ANNOTS" );

  logger << "  writing annotations (" 
	 << ( annot_format ? ".annot" : ".xml" ) 
	 << " format) to " 
	 << filename << "\n";

  std::ofstream O1( filename.c_str() , std::ios::out );
  
  if ( xml_format ) 
    {
      
      // XML header
      
      O1 << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n\n";
      O1 << "<Annotations>\n\n";
      O1 << "<SoftwareVersion>luna-" << globals::version << "</SoftwareVersion>\n\n";
      
      O1 << "<StartTime>" << start_hms << "</StartTime>\n";
      O1 << "<Duration>" << duration_hms << "</Duration>\n";
      O1 << "<DurationSeconds>" << duration_sec << "</DurationSeconds>\n";      
      O1 << "<EpochLength>" << epoch_sec << "</EpochLength>\n";
      
      O1 << "\n";
      
      //
      // Loop over each annotation
      //
      
      std::vector<std::string> anames = names();

      //
      // Track all instances (in order)      
      //

      std::set<instance_idx_t> events;
      
      //
      // Annotation header
      //
      
      O1 << "<Classes>\n";
      
      for (int a=0;a<anames.size();a++)
	{
	  
	  //   <Class>
	  //    <Name>Annotation Name</Name>
	  //    <Description>Annotation Description</Description>
	  //     <Variable name="label" type="type">Numeric variable name</Variable>
	  //     <Variable name="label" type="type">Numeric variable name</Variable>
	  //     <Variable name="label" type="type">Numeric variable name</Variable>
	  //   </Class>
	  
	  annot_t * annot = find( anames[a] );
	  
	  if ( annot == NULL ) continue;
	  
	  O1 << "<Class name=\"" << annot->name << "\">\n"
	     << " <Description>" << annot->description << "</Description>\n";
	  
	  std::map<std::string, globals::atype_t>::const_iterator aa = annot->types.begin();
	  while ( aa != annot->types.end() )
	    {
	      O1 << "  <Variable type=\"" 
		 << globals::type_name[ aa->second ] 
		 << "\">" 
		 << aa->first 
		 << "</Variable>\n";
	      ++aa;
	    }
	  
	  O1 << "</Class>\n\n";
	  

	  //
	  // Enter all events in to a single table
	  //

	  annot_map_t::const_iterator ii = annot->interval_events.begin();
          while ( ii != annot->interval_events.end() )
            {
              const instance_idx_t & instance_idx = ii->first;
	      events.insert( instance_idx );
	      ++ii;
	    }
	  

	  //
	  // Next annotation/class header
	  // 
	}
      
      O1 << "</Classes>\n\n";  
      
      
      //
      // Loop over all annotation instances
      //
      
      O1 << "<Instances>\n\n";
  
  
      //   <Instance>   
      //      <Class>Recording Start Time</Class>
      //      <Name>Recording Start Time</Name>
      //      <Start>0</Start>
      //      <Duration>32820.0</Duration>
      //      <Channel>Optional channel label(s)</Channel>
      //      <Value var="name">0.123</Value>
      //      <Value var="name">0.123</Value>
      //      <Value var="name">0.123</Value>
      //    </Instance>


      std::set<instance_idx_t>::const_iterator ee = events.begin();
      while ( ee != events.end() )
	{

	  const instance_idx_t & instance_idx = *ee;
	  
	  const annot_t * annot = instance_idx.parent;

	  annot_map_t::const_iterator ii = annot->interval_events.find( instance_idx );
          if ( ii == annot->interval_events.end() )  continue;
	  instance_t * inst = ii->second;
	  
	  
	  O1 << "<Instance class=\"" << annot->name << "\">\n";
	  
	  if ( instance_idx.id != "." && instance_idx.id != "" ) 
	    O1 << " <Name>" << instance_idx.id << "</Name>\n";
	  
	  O1 << " <Start>" << instance_idx.interval.start_sec() << "</Start>\n"
	     << " <Duration>" << instance_idx.interval.duration_sec() << "</Duration>\n";
	  
	  // name : instance_idx.id
	  // start : instance_idx.interval.start_sec()
	  // duration : instance_idx.interval.duration_sec()
	  
	  std::map<std::string,avar_t*>::const_iterator dd = inst->data.begin();
	  
	  while ( dd != inst->data.end() )
	    {
	      // var-name : dd->first
	      // value : 
	      
	      O1 << " <Value name=\"" << dd->first << "\">" 
		 << *dd->second 
		 << "</Value>\n"; 
	      ++dd;
	    }
	  
	  O1 << "</Instance>\n\n";


	  //
	  // Next instance (from events)
	  //

	  ++ee;

	}
      
      //
      // End of all annotation instatances
      //
      
      O1 << "</Instances>\n\n";

      //
      // Root node, close out the XML
      //
      
      O1 << "</Annotations>\n";
    }


  if ( annot_format ) 
    {

      //
      // Track all instances (in order)      
      //
      
      std::set<instance_idx_t> events;
      
      //
      // Annotation header
      //
      
      std::vector<std::string> anames = names();      
      
      for (int a=0;a<anames.size();a++)
	{
	  
	  annot_t * annot = find( anames[a] );
	  
	  if ( annot == NULL ) continue;

	  bool has_vars = annot->types.size() > 0 ;

	  O1 << "# " << annot->name;
	  
	  if ( annot->description != "" )
	    O1 << " | " << annot->description;
	  else if ( has_vars ) // need a dummy description here 
	    O1 << " | " << annot->description ;
	  
	  if ( has_vars ) 
	    O1 << " |";
	  
	  std::map<std::string, globals::atype_t>::const_iterator aa = annot->types.begin();
	  while ( aa != annot->types.end() )
	    {
	      O1 << " " << aa->first 
		 << "[" 
		 << globals::type_name[ aa->second ] 
		 << "]";	      
	      ++aa;
	    }
	  
	  O1 << "\n";


	  //
	  // Enter all events in to a single table
	  //
	  
	  annot_map_t::const_iterator ii = annot->interval_events.begin();
          while ( ii != annot->interval_events.end() )
            {
              const instance_idx_t & instance_idx = ii->first;
	      events.insert( instance_idx );
	      ++ii;
	    }
	  
	  //
	  // Next annotation class
	  //

	}

      //
      // dummy markers first 
      //

      // O1 << "<StartTime>" << start_hms << "</StartTime>\n";
      // O1 << "<Duration>" << duration_hms << "</Duration>\n";
      // O1 << "<DurationSeconds>" << duration_sec << "</DurationSeconds>\n";
      // O1 << "<EpochLength>" << epoch_sec << "</EpochLength>\n";

      if ( start_hms != "." ) 
	O1 << "# start_hms | EDF start time\n";
      if ( duration_hms != "." )
	O1 << "# duration_hms | EDF duration (hh:mm:ss)\n";
      if ( duration_sec != 0 )
	O1 << "# duration_sec | EDF duration (seconds)\n";
      if ( epoch_sec != 0 )
	O1 << "# epoch_sec | Default epoch duration (seconds)\n";

      if ( start_hms != "." )  
 	O1 << "start_hms\t" << start_hms << "\t" << 0 << "\t" << 0 << "\n";

      if ( duration_hms != "." )
	O1 << "duration_hms\t" << duration_hms << "\t0\t0\n";
      if ( duration_sec != 0 )
	O1 << "duration_sec\t" << duration_sec << "\t0\t0\n";
      if ( epoch_sec != 0 )
	O1 << "epoch_sec\t" << epoch_sec << "\t0\t0\n";

      //
      // Loop over all annotation instances
      //
      
      std::set<instance_idx_t>::const_iterator ee = events.begin();
      while ( ee != events.end() )
	{

	  const instance_idx_t & instance_idx = *ee;
	  
	  const annot_t * annot = instance_idx.parent;

	  if ( annot == NULL ) { ++ee; continue; } 
	  
	  annot_map_t::const_iterator ii = annot->interval_events.find( instance_idx );
          if ( ii == annot->interval_events.end() )  { ++ee; continue; } 
	  instance_t * inst = ii->second;

	  O1 << annot->name << "\t";

	  if ( instance_idx.id != "." && instance_idx.id != "" ) 
	    O1 << instance_idx.id << "\t";
	  else 
	    O1 << ".\t";
	  
	  O1 << instance_idx.interval.start_sec() << "\t"
	     << instance_idx.interval.stop_sec();
	  
	  std::map<std::string,avar_t*>::const_iterator dd = inst->data.begin();
	  
	  while ( dd != inst->data.end() )
	    {
	      O1 << "\t" << *dd->second;
	      ++dd;
	    }
	  
	  O1 << "\n";

	  //
	  // Next instance/event
	  //

	  ++ee;
	  
	}
      
    }
        
  //
  // All done
  //
  
  O1.close();
    

}




bool annot_t::loadxml_luna( const std::string & filename , edf_t * edf )
{
  
  XML xml( filename );
  
  if ( ! xml.valid() ) Helper::halt( "invalid annotation file: " + filename );

  //
  // Annotation classes
  //
  
  std::vector<element_t*> classes = xml.children( "Classes" );

  std::set<std::string> added;
  
  for (int i=0;i<classes.size();i++)
    {
      
      element_t * cls = classes[i];
      
      if ( ! Helper::iequals( cls->name , "Class" ) ) continue;
      
      std::string cls_name = cls->attr.value( "name" );
      
      //
      // alias remapping?
      //

      std::string original_label = cls_name;
      cls_name = nsrr_t::remap( cls_name );
      
      //
      // ignore this annotation?
      //
      
      if ( globals::specified_annots.size() > 0 && 
	   globals::specified_annots.find( cls_name )
	   == globals::specified_annots.end() ) continue;

      
      if ( cls_name != original_label )
	edf->timeline.annotations.aliasing[ cls_name ] = original_label ;
      
      std::string desc = "";
      std::map<std::string,std::string> atypes;

      std::vector<element_t*> kids = cls->child;
      
      for (int j=0; j<kids.size(); j++)
        {
	  
          const std::string & key = kids[j]->name;

	  if ( key == "Description" ) 
	    {
	      desc = kids[j]->value;
	    }
	  else if ( key == "Variable" ) 
	    {
// 	      std::cout << "Var name = " << kids[j]->value << "\n";
// 	      std::cout << "Var type = " << kids[j]->attr.value( "type" ) << "\n";
	      atypes[ kids[j]->value ] = kids[j]->attr.value( "type" );
	    }
	  
        }

      
      //       <Class name="a3">
      // 	  <Name>a3</Name>
      // 	  <Description>This annotation also specifies meta-data types</Description>
      // 	  <Variable type="txt">val1</Variable>
      // 	  <Variable type="num">val2</Variable>
      // 	  <Variable type="bool">val3</Variable>
      //       </Class>
      
      //
      // add this annotation
      //
      
      annot_t * a = edf->timeline.annotations.add( cls_name );
      
      a->description = desc;
      a->file = filename;
      a->type = globals::A_FLAG_T; // not expecting any meta-data (unless changed below)

      std::map<std::string,std::string>::const_iterator aa = atypes.begin();
      while ( aa != atypes.end() )
	{
	  // if a recognizable type, add
	  if ( globals::name_type.find( aa->second ) != globals::name_type.end() )
	    a->types[ aa->first ] = globals::name_type[ aa->second ];
	  ++aa;
	}
      
      // as with .annot files; if only one variable, set annot_t equal to the one instance type
      // otherwise, set as A_NULL_T ; in practice, don't think we'll ever use annot_t::type 
      // i.e. will always use annot_t::atypes[]

      if ( a->types.size() == 1 ) a->type = a->types.begin()->second;
      else if ( a->type > 1 ) a->type = globals::A_NULL_T; 
      // i.e. multiple variables/types set, so set overall one to null

    }


  //
  // Annotation Instances
  //
  
  std::vector<element_t*> instances = xml.children( "Instances" );
  

  //
  // First pass through all instances
  //
  
  for (int i=0; i<instances.size(); i++) 
    {
      
      element_t * ii = instances[i];
      
      std::string cls_name = ii->attr.value( "class" );

      //
      // alias remapping?
      //

      std::string original_label = cls_name;
      cls_name = nsrr_t::remap( cls_name );
      
      //
      // ignore this annotation?
      //
           
      if ( globals::specified_annots.size() > 0 && 
	   globals::specified_annots.find( cls_name ) 
	   == globals::specified_annots.end() ) continue;

      if ( cls_name != original_label )
	edf->timeline.annotations.aliasing[ cls_name ] = original_label ;

      
      //
      // get a pointer to this class
      //

      annot_t * a = edf->timeline.annotations.find( cls_name );
      
      if ( a == NULL ) continue;
      
      // pull information for this instance:
      
      element_t * name     = (*ii)( "Name" );
      
      element_t * start    = (*ii)( "Start" );
      
      element_t * duration = (*ii)( "Duration" );
      
      //
      // Get time interval
      //

      double dbl_start = 0 , dbl_dur = 0 , dbl_stop = 0;
      
      if ( ! Helper::str2dbl( start->value , &dbl_start ) )
	Helper::halt( "invalid interval: " + start->value );
		  
      if ( ! Helper::str2dbl( duration->value , &dbl_dur ) ) 
	Helper::halt( "invalid interval: " +  duration->value );
      
      dbl_stop = dbl_start + dbl_dur; 
	      
      if ( dbl_start < 0 ) Helper::halt( filename + " contains row(s) with negative time points" ) ;

      if ( dbl_dur < 0 ) Helper::halt( filename + " contains row(s) with negative durations" );
      
      // convert to uint64_t time-point units
      
      interval_t interval;

      interval.start = Helper::sec2tp( dbl_start );
      
      // assume stop is already specified as 1 past the end, e.g. 30 60
      // *unless* it is a single point, e.g. 5 5 
      // which is handled below
      
      interval.stop  = Helper::sec2tp( dbl_stop );

      
      // given interval encoding, we always want one past the end
      // if a single time-point given (0 duration)
      //
      // otherwise, assume 30 second duration means up to 
      // but not including 30 .. i..e  0-30   30-60   60-90 
      // in each case, start + duration is the correct value

      if ( interval.start == interval.stop ) ++interval.stop;

      //
      // Create the instance
      //
      
      instance_t * instance = a->add( name ? name->value : "." , interval );

      //
      // Add any additional data members
      //

      std::vector<element_t*> kids = ii->child;
      
      for (int j=0; j<kids.size(); j++) 
	{
	  
	  const std::string & key = kids[j]->name;
	  
	  if ( key == "Value" ) 
	    {
	      std::string var = kids[j]->attr.value( "name" );
	      std::string val = kids[j]->value;
	      
	      if ( a->types.find( var ) != a->types.end() ) 
		{
		  
		  globals::atype_t t = a->types[ var ];
	      
		  if ( t == globals::A_FLAG_T ) 
		    {
		      instance->set( var );
		    }
		  
		  else if ( t == globals::A_MASK_T )
		    {
		      if ( var != "." )
			{
			  // accepts F and T as well as long forms (false, true)
			  instance->set_mask( var , Helper::yesno( val ) );
			}
		    }
		  
		  else if ( t == globals::A_BOOL_T )
		    {
		      if ( val != "." )
			{
			  // accepts F and T as well as long forms (false, true)
			  instance->set( var , Helper::yesno( val ) );
			}
		    }
		  
		  else if ( t == globals::A_INT_T )
		    {
		      int value = 0;
		      if ( ! Helper::str2int( val , &value ) )
			Helper::halt( "bad numeric value in " + filename );
		      instance->set( var , value );
		    }

		  else if ( t == globals::A_DBL_T )
		    {
		      double value = 0;
		      
		      if ( Helper::str2dbl( val , &value ) )		    
			instance->set( var , value );
		      else
			if ( var != "." && var != "NA" ) 
			  Helper::halt( "bad numeric value in " + filename );		  
		    }
		  
		  else if ( t == globals::A_TXT_T )
		    {
		      instance->set( var , val );
		    }
		  
		}

	    } // added this data member
	  
	}
     
      //
      // Next instance
      //
    }
  
  return true;
}



void annotation_set_t::clear() 
{ 
  std::map<std::string,annot_t*>::iterator ii = annots.begin();
  while ( ii != annots.end() ) 
    {
      delete ii->second;
      ++ii;
    }
  
  annots.clear(); 
  
  start_ct.reset();
  
  start_hms = ".";
  
  duration_hms = ".";
  
  duration_sec = 0 ;
  
  epoch_sec = 0 ; 
  
}


//
// Initiate annotation set from EDF, to seed with a few key values
//

void annotation_set_t::set( edf_t * edf ) 
{
  // populate start_hms
  // duration_hms,
  // duration_sec
  // and epoch_sec

  
  if ( edf != NULL )
    {
      
      duration_sec = edf->header.nr_all * edf->header.record_duration ;
      
      duration_hms = Helper::timestring( globals::tp_1sec * duration_sec );
      
      clocktime_t etime( edf->header.starttime );
      
      if ( etime.valid )
	{

	  start_ct = etime;
	  start_hms = edf->header.starttime ;
	  
	  // double time_hrs = ( edf->timeline.last_time_point_tp * globals::tp_duration ) / 3600.0 ;
	  // etime.advance( time_hrs );	  
	}
      
      epoch_sec = edf->timeline.epoched() ?
	edf->timeline.epoch_length() :
	globals::default_epoch_len ; 
      
    }
  
}


//
// Convert from EDF Annotations track(s) to Luna-format annotations
//

annot_t * annotation_set_t::from_EDF( edf_t & edf )
{
  
  logger << "  extracting 'EDF Annotations' track\n";
  

  // create a single annotation (or bind to it, if it already exists)
  
  annot_t * a = edf.timeline.annotations.add( globals::edf_annot_label );
  
  a->name = globals::edf_annot_label;
  a->description = "EDF Annotations";
  a->file = edf.filename;
  a->type = globals::A_FLAG_T; 

  int r = edf.timeline.first_record();
  
  while ( r != -1 )
    {

      for ( int s = 0 ; s < edf.header.ns; s ++ )
	{
	  
	  if ( edf.header.is_annotation_channel( s ) )
	    {	      
	      
	      tal_t t = edf.tal( s , r );
	      
	      const int na = t.size();
	      
	      for (int i=0; i<na; i++)
		{
		  
		  tal_element_t & te = t.d[i];
		  
		  if ( te.name != globals::edf_timetrack_label )
		    {
		      
		      uint64_t start_tp = Helper::sec2tp( te.onset );

		      uint64_t dur_tp = Helper::sec2tp( te.duration );
		      
		      // stop is one past the end 
		      uint64_t stop_tp  = start_tp + dur_tp ;

		      // ensure at least one tp (i.e. zero-length annotation is (a,a+1)
		      if ( stop_tp == start_tp ) stop_tp += 1LLU;
		      
		      interval_t interval( start_tp , stop_tp );
		      
		      instance_t * instance = a->add( Helper::trim( te.name ) , interval );
		      
		      //std::cerr << " adding [" << te.name << "] -- " << te.onset << "\t" << interval.duration() << "\n";
		      
		      // track how many annotations we add
		      edf.aoccur[ globals::edf_annot_label ]++;
		    }
		  
		}

	    } 
	  
	} // next signal

      r = edf.timeline.next_record( r );
      
    } // next record
  
  return a;
}

