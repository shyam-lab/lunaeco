
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

#include "artifacts.h"

#include "edf/edf.h"
#include "edf/slice.h"
#include "annot/annot.h"

#include "helper/helper.h"
#include "helper/logger.h"
#include "eval.h"
#include "db/db.h"

#include "dsp/resample.h"
#include "dsp/mse.h"
#include "dsp/lzw.h"

#include "miscmath/miscmath.h"
#include "fftw/fftwrap.h"

#include <iostream>
#include <fstream>

extern writer_t writer;

extern logger_t logger;

annot_t * brunner_artifact_detection( edf_t & edf , 
				      const std::string & signal_label , 
				      const std::string & filename )
{
  

  Helper::halt("brunner artifact detection: not implemented" );
  
  //
  // Brunner et al. (1996) power spectra:
  //

  // FFT on 4-sec intervals, with Hamming window
  // 0.25Hz bins collapsed to
  //  0.5Hz bins between 0.25 and 20.0Hz
  //  1Hz bins between 20.25 and 32.0Hz
  // -->results in 52 bins per 4-sec epoch
    
  return NULL;
}



annot_t * buckelmuller_artifact_detection( edf_t & edf , 
					   param_t & param , 
					   const std::string & signal_label , 
					   const double delta_threshold , 
					   const double beta_threshold , 
					   const double delta_lwr , 
					   const double delta_upr ,
					   const double beta_lwr , 
					   const double beta_upr ,
					   const std::string & filename )
{


  //
  // parameters
  //
  
  bool set_mask = ! param.has( "no-mask" );
  bool verbose = param.has("verbose") || param.has( "epoch" );
  
  //
  // attach signal
  //
  
  signal_list_t signals = edf.header.signal_list( signal_label );  

  const int ns = signals.size();

  //
  // Sampling frequencies
  //

  std::vector<double> Fs = edf.header.sampling_freq( signals );


  //
  // Point to first epoch (assume 30 seconds, but could be different)
  //
  
  int ne = edf.timeline.first_epoch();

 
  //
  // Store per-epoch power
  //

  std::vector<std::vector<double> > delta(ns);
  std::vector<std::vector<double> > beta(ns);

    
  //
  // for each each epoch 
  //

  int cnt = 0;
  std::vector<int> track_epochs;
  
  while ( 1 ) 
    {
      
      int epoch = edf.timeline.next_epoch();      
       
      if ( epoch == -1 ) break;
      
      track_epochs.push_back( epoch );
      
      //
      // Get data 
      //

      interval_t interval = edf.timeline.epoch( epoch );

      for ( int s=0; s<ns; s++ )
	{

	  //
	  // only consider data tracks
	  //
	  
	  if ( edf.header.is_annotation_channel( signals(s) ) ) continue;	  
	  
	  slice_t slice( edf , signals(s) , interval );
	  
	  std::vector<double> * d = slice.nonconst_pdata();

	  const std::vector<uint64_t> * tp = slice.ptimepoints();
	  
	  //
	  // Mean-centre 30-second window
	  //

	  MiscMath::centre( d );
            
	  //
	  // Apply PWELCH to this epoch
	  //

	  // aim to get 10 windows of 4 seconds in 30sec epoch
	  
	  int noverlap_segments = 10;         
	  int segment_size_sec = 4;
	  
	  PWELCH pwelch( *d , Fs[s] , segment_size_sec , noverlap_segments );
      
	  // track power bands     
	  
	  delta[s].push_back( pwelch.psdsum( DELTA ) );
	  beta[s].push_back( pwelch.psdsum( beta_lwr  , beta_upr ) );
	  

	} // next signal

    } // next epoch



  
  //
  // Report for each signal
  //
  
  std::vector<std::vector<double> > delta_average(ns);
  std::vector<std::vector<double> > beta_average(ns);
  
  for (int s=0;s<ns;s++)
    {

      //
      // only consider data tracks
      //
      
      if ( edf.header.is_annotation_channel( signals(s) ) ) continue;

      //
      // Output stratifier
      //

      writer.level( signals.label(s) , globals::signal_strat );


      //
      // Make running averages
      //
      
      delta_average[s] = MiscMath::moving_average( delta[s] , 15 );
      beta_average[s]  = MiscMath::moving_average( beta[s]  , 15 );
      
      int total = 0, altered = 0;

      for (int e=0;e<ne;e++)
	{
	  
	  double dfac = delta[s][e] / delta_average[s][e];
	  double bfac = beta[s][e] / beta_average[s][e];
	  
	  bool dmask = dfac > delta_threshold ;
	  bool bmask = bfac > beta_threshold ;
	  bool mask  = dmask || bmask;
	  
	  if ( verbose ) 
	    {
	      
	      writer.epoch( edf.timeline.display_epoch( track_epochs[e] ) );

	      writer.var( "DELTA" , "Delta power" );
	      writer.var( "DELTA_AVG" , "Local average delta power" );
	      writer.var( "DELTA_FAC" , "Relative delta power factor" );
	      writer.var( "BETA" , "Beta power" );
	      writer.var( "BETA_AVG" , "Local average beta power" );
	      writer.var( "BETA_FAC" , "Relative beta power factor" );
	      writer.var( "DELTA_MASK" , "Masked based on delta power" );
	      writer.var( "BETA_MASK" , "Masked based on beta power" );
	      writer.var( "MASK" , "Masked" );		

	      writer.value( "DELTA" , delta[s][e] );
	      writer.value( "DELTA_AVG" , delta_average[s][e] );
	      writer.value( "DELTA_FAC" , dfac );

	      writer.value( "BETA" , beta[s][e] );
	      writer.value( "BETA_AVG" , beta_average[s][e] );
	      writer.value( "BETA_FAC" , bfac );
	      
	      writer.value( "DELTA_MASK" , dmask );
	      writer.value( "BETA_MASK" , bmask );
	      writer.value( "MASK" , mask );

	    }
	  
	  //
	  // Mask this epoch?
	  //
	  
	  if ( set_mask && mask )
	    {
	      if ( !edf.timeline.masked(e) ) ++altered;
	      edf.timeline.set_epoch_mask( e );
	      ++total;
	    }


	  writer.unepoch();
	  
	}

      if ( set_mask )
	logger << " masked " << total << " of " << ne << " epochs, altering " << altered << "\n";
      
      // # masked (i.e. actually), # masked, # total
      // altered , 

      writer.var( "FLAGGED_EPOCHS" , "Number of epochs failing Buckelmueller" );
      writer.var( "ALTERED_EPOCHS" , "Number of epochs actually masked" ); 
      writer.var( "TOTAL_EPOCHS" , "Number of epochs tested" );
      
      writer.value( "FLAGGED_EPOCHS" , total );
      writer.value( "ALTERED_EPOCHS" , altered );
      writer.value( "TOTAL_EPOCHS" , ne );

      writer.unlevel( globals::signal_strat );
      
    } // next signal    
  
  

  //
  // For now, do not return any annot_t
  //
  
  return NULL;


  //
  // In future, we may routinely expand all functions to return annotations
  //
  
  annot_t * a = edf.timeline.annotations.add( "Buckelmuller" );
  
  if ( a == NULL ) std::cout << "is null;\n";

  a->description = "Buckelmuller et al (2006) automatic artifact detection" ;

  for (int s=0;s<ns;s++)
    {
      //
      // only consider data tracks
      //
      
      if ( edf.header.is_annotation_channel( signals(s) ) ) 
	continue;
      
      for (int e=0;e<ne;e++)
	{
	  double dfac = delta[s][e] / delta_average[s][e];
	  double bfac = beta[s][e] / beta_average[s][e];
	  
	  bool reject =  dfac > delta_threshold || bfac > beta_threshold;
	  
	  if ( reject ) 
	    a->add( "buckelmuller:" + signals.label(s)  , edf.timeline.epoch(e) );
	}
      
    }
  
  return a;
  
}



//
// SIGSTATS
//

void  rms_per_epoch( edf_t & edf , param_t & param )
{

  
  // Hjorth parameters: H1, H2, H3
  // Optional: RMS, % clipped signals
  // Turning rate 

  
  std::string signal_label = param.requires( "sig" );  

  bool verbose = param.has( "verbose" ) || param.has( "epoch") ;

  bool calc_rms = param.has( "rms" );

  bool calc_clipped = param.has( "clipped" );

  bool calc_flat = param.has( "flat" );

  bool calc_maxxed = param.has( "max" );


  // e.g. exclude EPOCH is more than 5% of points are clipped  
  double clip_threshold = calc_clipped ? param.requires_dbl( "clipped" ) : 0.05 ;
  if ( calc_clipped )
    logger << "  flagging epochs with " << clip_threshold << " proportion X[i] == max(X) or min(X)\n";

  double flat_threshold = 0.05;
  double flat_eps = 1e-6;
  if ( calc_flat )
    {
      std::vector<double> x = param.dblvector( "flat" );
      if ( x.size() != 1 && x.size() != 2 ) Helper::halt( "flat requires 1 or 2 param: flat=<pct>,<eps>" );
      flat_threshold = x[0];
      if ( x.size() == 2 ) flat_eps = x[1];
      logger << "  flagging epochs with " << flat_threshold << " proportion |X[i]-X[i-1]| < " << flat_eps << "\n";
    }
  
  double max_threshold = 0.05;
  double max_value = 0;
  if ( calc_maxxed )
    {
      std::vector<double> x = param.dblvector( "max" );
      if ( x.size() != 2 ) Helper::halt( "max requires 2 params: max=<value>,<pct>" );
      max_value = x[0];
      max_threshold = x[1];
      logger << "  flagging epochs with " << max_threshold << " proportion |X| > " << max_value << "\n";
    }
  
  //
  // Calculate channel-level statistics?
  //

  bool cstats = param.has( "cstats" ) ;

  double ch_th = cstats ? param.requires_dbl( "cstats" ) : 0 ;

  bool cstats_all = ! param.has( "cstats-unmasked-only" );
  
  
  //
  // Optionally calculate turning rate
  //
  
  bool turning_rate = param.has( "tr" )  || param.has( "tr-epoch" ) || param.has( "tr-d" ) || param.has( "tr-smooth" );

  double tr_epoch_sec = 1.0;
  int    tr_d = 4;
  int    tr_epoch_smooth = 30; // +1 is added afterwards
  
  if ( turning_rate ) 
    {
      if ( param.has( "tr-epoch" ) ) tr_epoch_sec = param.requires_dbl( "tr-epoch" );
      if ( param.has( "tr-d" ) ) tr_d = param.requires_int( "tr-d" );
      if ( param.has( "tr-smooth" ) ) tr_epoch_smooth = param.requires_int( "tr-smooth" );
      logger << " calculating turning rate: d="<< tr_d << " for " << tr_epoch_sec << "sec epochs, smoothed over " << tr_epoch_smooth << " epochs\n";
    }


  //
  // allow for iterative outlier detection, i.e. with multiple
  // comma-delimited thresholds
  //
  
  bool apply_mask = false;

  std::vector<double> th;

  if ( param.has( "threshold" ) )
    {
      apply_mask = true;
      th = param.dblvector( "threshold" );
    }
  else if ( param.has( "th" ) )
    {
      apply_mask = true;
      th = param.dblvector( "th" );
    }

  // if only want to mask given CLIP, MAX, FLAT (i.e. not Hjorth)
  // need to add 'mask' option;   
  if ( param.has( "mask" ) )
    apply_mask = true;
  
  int th_nlevels = th.size();
  
  //
  // channel/epoch (chep) masks; this will not set any full epoch-masks, but it will 
  // populate timeline.chep masks (i.e. that can be subsequently turned into lists of bad channels/epochs
  // and also used to interpolate signals
  //

  bool chep_mask = param.has( "chep" );

  // cannot apply both chep and mask options
  if ( chep_mask )
    {
      // check a masking threshold was specified above

      if ( ! apply_mask )
	Helper::halt( "no threshold ('th') specified for chep" );
      
      // cannot apply epoch mask and chep mask together,
      // so now set this to F anyway
      
      apply_mask = false;
    }

  //
  // All-Epochs
  //

  // make missing based on distribution of ALL epochs and ALL channels
  // do this indpendent of CHEP or CSTATS for now, or standard th= mask
  
  bool astats = param.has( "astats" );
  if ( astats && chep_mask ) Helper::halt( "cannot specify astats and chep" ) ;
  if ( astats && cstats ) Helper::halt( "cannot specify astats and cstats" ) ;
  if ( astats && apply_mask ) Helper::halt( "cannot specify astats and th" ) ;
  
  std::vector<double> astats_th = param.dblvector( "astats" );


  //
  // Calculate per-EPOCH, and also signal-wide, the signal RMS 
  // Also calculate the proportion of flat/clipped signals (calculated per-EPOCH)
  //
  
  //
  // Attach signals
  //
  
  signal_list_t signals = edf.header.signal_list( signal_label );  

  // all channels
  const int ns_all = signals.size();

  // data channels
  std::vector<int> sdata;
  for (int s=0;s<ns_all;s++)
    if ( ! edf.header.is_annotation_channel( signals(s) ) )
      sdata.push_back(s);

  int ns = 0;
  for (int s=0;s<ns_all;s++)
    if ( ! edf.header.is_annotation_channel( signals(s) ) ) ++ns;

  //  std::cerr << "ns= " << ns << " " << ns_all << "\n";

  if ( ns == 0 ) return;

  //
  // Store per-epoch statistics
  //

  std::vector<int>      n( ns , 0 );

  std::vector<double> rms( ns , 0 );
  std::vector<double> clipped( ns , 0 );
  std::vector<double> flat( ns , 0 );
  std::vector<double> maxxed( ns , 0 );
  std::vector<double> mean_activity( ns , 0 );
  std::vector<double> mean_mobility( ns , 0 );
  std::vector<double> mean_complexity( ns , 0 );
  std::vector<double> mean_turning_rate( ns , 0 ); // nb. this is based on turning-rate epoch size


  //
  // Track original data, if calculating outliers, e_*, i.e. EPOCH level
  //
  
  std::vector<std::vector<double> > e_rms;
  std::vector<std::vector<double> > e_clp;
  std::vector<std::vector<double> > e_flt;
  std::vector<std::vector<double> > e_max;
  std::vector<std::vector<double> > e_act;
  std::vector<std::vector<double> > e_mob;
  std::vector<std::vector<double> > e_cmp;
  std::vector<std::vector<int> > e_epoch;
  std::vector<std::vector<double> > e_tr;

  if ( apply_mask || cstats || chep_mask || astats )
    {
      e_rms.resize( ns );
      e_clp.resize( ns );
      e_flt.resize( ns );
      e_max.resize( ns );
      e_act.resize( ns );
      e_mob.resize( ns );
      e_cmp.resize( ns );
      e_epoch.resize( ns );
    }

  if ( turning_rate )
    e_tr.resize( ns );
  
  //
  // Point to first epoch 
  //
  
  int ne = edf.timeline.first_epoch();

  if ( ne == 0 ) return;
  
  
  //
  // For each signal
  //

  int si = -1;

  for (int s=0;s<ns_all;s++)
    {

      //
      // only consider data tracks
      //
      
      if ( edf.header.is_annotation_channel( signals(s) ) ) continue;

      ++si;

      //
      // output stratifier (only needed at this stage if verbose, epoch-level output will
      // also be written)
      //

      if ( verbose ) 
	writer.level( signals.label(s) , globals::signal_strat );

      //
      // reset to first epoch
      //
      
      edf.timeline.first_epoch();

      //
      // Get sampling rate
      //
      
      int sr = edf.header.sampling_freq( s );
      
      //
      // for each each epoch 
      //
      
      while ( 1 ) 
	{
	  
	  //
	  // Get next epoch
	  //
	  
	  int epoch = edf.timeline.next_epoch();
	  
	  if ( epoch == -1 ) break;
	  
	  interval_t interval = edf.timeline.epoch( epoch );
	  	  
	  slice_t slice( edf , signals(s) , interval );
	  
	  std::vector<double> * d = slice.nonconst_pdata();

	  //
	  // get clipped, flat and/or maxxed points (each is a proportion of points in the epoch)
	  //

	  double c = calc_clipped ? MiscMath::clipped( *d ) : 0 ;

	  double f = calc_flat ? MiscMath::flat( *d , flat_eps ) : 0 ;
	  
	  double m = calc_maxxed ? MiscMath::max( *d , max_value ) : 0 ; 


	  //
	  // Mean-centre 30-second window, calculate RMS
	  //

	  MiscMath::centre( d );
	  
	  double x = calc_rms ? MiscMath::rms( *d ) : 0 ;
	  	  
	  

	  //
	  // Hjorth parameters
	  //

	  double activity = 0 , mobility = 0 , complexity = 0;
	  
	  MiscMath::hjorth( d , &activity , &mobility , &complexity );


	  //
	  // Turning rate
	  //
	  
	  double turning_rate_mean = 0;

	  if ( turning_rate )
	    {

	      std::vector<double> subepoch_tr;
	      
	      turning_rate_mean = MiscMath::turning_rate( d , sr, tr_epoch_sec , tr_d , &subepoch_tr );
	      
	      for (int i=0;i<subepoch_tr.size();i++)
		e_tr[s].push_back( subepoch_tr[i] );
	    }

	  //
	  // Verbose output
	  //
	  
	  if ( verbose )
	    {

	      writer.epoch( edf.timeline.display_epoch( epoch ) );

	     
	      //
	      // Report calculated values
	      //
  
	      writer.value( "H1" , activity , "Epoch Hjorth parameter 1: activity (variance)" );
	      writer.value( "H2" , mobility , "Epoch Hjorth parameter 2: mobility" );
	      writer.value( "H3" , complexity , "Epoch Hjorth parameter 3: complexity" );

	      if ( calc_rms )
		writer.value( "RMS" , x , "Epoch root mean square (RMS)" );
	      
	      if ( calc_clipped )
		writer.value( "CLIP" , c , "Proportion of epoch with clipped signal" );

	      if ( calc_flat )
		writer.value( "FLAT" , f , "Proportion of epoch with flat signal" );

	      if ( calc_maxxed )
		writer.value( "MAX" , c , "Proportion of epoch with maxed signal" );
	      
	      if ( turning_rate ) 
		writer.value( "TR" , turning_rate_mean , "Turning rate mean per epoch" );
	    }


	  //
	  // Tot up for individual-level means
	  //

	  if ( calc_rms )
	    rms[si] += x;

	  if ( calc_clipped )
	    clipped[si] += c;
	  
	  if ( calc_flat )
	    flat[si] += f;

	  if ( calc_maxxed )
	    maxxed[si] += m;
	  
	  mean_activity[si] += activity;
	  mean_mobility[si] += mobility;
	  mean_complexity[si] += complexity;

	  n[si]   += 1;
	
	  
	  //
	  // Track for thresholding, or channel-stats ?
	  //

	  if ( apply_mask || cstats || chep_mask || astats ) 
	    {
	      if ( calc_rms ) 
		e_rms[si].push_back( x );

	      if ( calc_clipped)
		e_clp[si].push_back( c );

	      if ( calc_flat )
		e_flt[si].push_back( f );
	      
	      if ( calc_maxxed )
		e_max[si].push_back( m );

	      e_act[si].push_back( activity );
	      e_mob[si].push_back( mobility );
	      e_cmp[si].push_back( complexity );
	      
	      e_epoch[si].push_back( epoch );
	    }
	
	
	  //
	  // Next epoch
	  //

	} 

      if ( verbose ) 
	writer.unepoch();
      
      //
      // Next signal
      //
      
    } 

  if ( verbose )
    writer.unlevel( globals::signal_strat );


  
  //
  // Phase 2: within each signal, find outlier epochs and mask
  //
  
  if ( apply_mask || chep_mask ) 
    {

      // s   selectedchannel (i.e. included annot tracks, all signals())
      // si  only data channels, i.e. 'real' ns for downstream matrices

      int si = -1;

      for (int s=0;s<ns_all;s++)
	{
	  	  
	  //
	  // only consider data tracks
	  //
	  
	  if ( edf.header.is_annotation_channel( signals(s) ) ) continue;

	  ++si;

	  int cnt_rms = 0 , cnt_clp = 0 ,
	    cnt_flt = 0 , cnt_max = 0 ,
	    cnt_act = 0 , cnt_mob = 0 , cnt_cmp = 0;
	  
	  if ( n[si] < 2 ) continue;
	  
	  writer.level( signals.label(s) , globals::signal_strat );

	  //	  std::cout << "n[s] = " << si << " " << n[si] << "\n";
	  
	  // for each iteration of outlier pruning
	  // track which epochs we are dropping here
	  std::vector<bool> dropped( n[si] , false ); 
	  
	  // total number of epochs dropped
	  int total = 0;
	  int altered = 0;

	  // masking only on FLAT/CLIP or MAX?
	  bool no_hjorth = apply_mask && th_nlevels == 0 ;

	  int iters = no_hjorth ? 1 : th_nlevels ;
	  for (int o=0; o < iters ; o++)
	    {
	      
	      const int nepochs = n[si];

	      std::vector<double> act_rms;
	      std::vector<double> act_act;
	      std::vector<double> act_mob;
	      std::vector<double> act_cmp;

	      // calculate current mean for RMS and Hjorth parameters
	      // (clipping based on a single fixed threshold, not statistically)

	      for (int j=0;j<nepochs;j++)
		{
		  if ( ! dropped[j] ) 
		    {
		      if ( calc_rms )
			act_rms.push_back( e_rms[si][j] );
		      
		      act_act.push_back( e_act[si][j] );
		      act_mob.push_back( e_mob[si][j] );
		      act_cmp.push_back( e_cmp[si][j] );
		    }
		}

	      double mean_rms = calc_rms ? MiscMath::mean( act_rms ) : 0 ;
	      double mean_act = MiscMath::mean( act_act );
	      double mean_mob = MiscMath::mean( act_mob );
	      double mean_cmp = MiscMath::mean( act_cmp );
	      
	      double sd_rms = calc_rms ? MiscMath::sdev( act_rms , mean_rms ) : 1;
	      double sd_act = MiscMath::sdev( act_act , mean_act ); 
	      double sd_mob = MiscMath::sdev( act_mob , mean_mob ); 
	      double sd_cmp = MiscMath::sdev( act_cmp , mean_cmp ); 

	      double this_th = no_hjorth ? 0 : th[o];
	      
	      double lwr_rms = calc_rms ? mean_rms - this_th * sd_rms : 0 ;
	      double lwr_act = mean_act - this_th * sd_act;
	      double lwr_mob = mean_mob - this_th * sd_mob;
	      double lwr_cmp = mean_cmp - this_th * sd_cmp;
	  
	      double upr_rms = calc_rms ? mean_rms + this_th * sd_rms : 0 ; 
	      double upr_act = mean_act + this_th * sd_act;
	      double upr_mob = mean_mob + this_th * sd_mob;
	      double upr_cmp = mean_cmp + this_th * sd_cmp;

	      if ( ! no_hjorth ) 
		logger << "  RMS/Hjorth filtering " << edf.header.label[ signals(s) ] << ", threshold +/-" << th[o] << " SDs";
	      else
		logger << "  Fixed threshold filtering " << edf.header.label[ signals(s) ] ;
	      
	      const int ne = e_epoch[si].size();

	      //	      std::cerr << "comp = " << ne << " " << nepochs << "\n";
	      
	      int total_this_iteration = 0;
	      
	      for (int ei=0; ei<ne; ei++)
		{
		  
		  // track which epochs were actually used
		  const int e = e_epoch[si][ei];
		  
		  // skip if this epoch is already masked
		  if ( dropped[ei] ) continue;
		  
		  bool set_mask = false;
		  		  
		  // For clipping/flat/max, use a fixed threshold
		  if ( calc_clipped && e_clp[si][ei] > clip_threshold ) 
		    {
		      set_mask = true;
		      cnt_clp++;
		    }
		  		  		  
		  if ( calc_flat && e_flt[si][ei] > flat_threshold ) 
		    {
		      set_mask = true;
		      cnt_flt++;
		    }

		  if ( calc_maxxed && e_max[si][ei] > max_threshold ) 
		    {
		      set_mask = true;
		      cnt_max++;
		    }

		  // For other metrics, use a (variable) statistical threshold (SD units)
		  if ( ! no_hjorth )
		    {

		      if ( calc_rms && ( e_rms[si][ei] < lwr_rms || e_rms[si][ei] > upr_rms ) ) 
			{
			  set_mask = true;
			  cnt_rms++;
			}
		      
		      if ( e_act[si][ei] < lwr_act || e_act[si][ei] > upr_act ) 
			{
			  set_mask = true;
			  cnt_act++;
			}
		      
		      if ( e_mob[si][ei] < lwr_mob || e_mob[si][ei] > upr_mob ) 
			{
			  set_mask = true;
			  cnt_mob++;
			}
		      
		      if ( e_cmp[si][ei] < lwr_cmp || e_cmp[si][ei] > upr_cmp )
			{
			  set_mask = true;
			  cnt_cmp++;
			}
		      
		    }
	
	          //
		  // full mask
		  //
		  
		  if ( set_mask ) 
		    {
		      
		      if ( chep_mask ) // channel/epoch mask rather than standard epoch mask?
			{
			  edf.timeline.set_chep_mask( e , signals(s) );
			  // do not set 'dropped' here... 
			  // i.e. we'll consider all epochs for subsequent
			  //      signals
			  ++total_this_iteration;
			  ++total;
			}
		      else
			{			  
			  if ( !edf.timeline.masked(e) ) ++altered;
			  edf.timeline.set_epoch_mask( e );		  
			  dropped[ei] = true;
			  ++total_this_iteration;
			  ++total;
			}
		    }
		  
		} // next epoch	  
	      
	      logger << ": removed " << total_this_iteration 
		     << " epochs";
	      if ( no_hjorth )
		logger << "\n";
	      else
		logger << " (iteration " << o+1 << ")\n";
	      	      
	    } // next outlier iteration
	  
	
	  //
	  // report final epoch-level masking
	  //
	  
	  if ( verbose )
	    {	      
	      const int ne = e_epoch[si].size();
	      for (int ei = 0 ; ei < ne ; ei++ )		
		{
		  writer.epoch( edf.timeline.display_epoch( e_epoch[si][ei] ) );
		  writer.value( "MASK" , dropped[ei] ? 1 : 0 , "Masked epoch? (1=Y)" ); 
		}
	      writer.unepoch();
	    }
	  
	  logger << " Overall, masked " << total << " of " << ne << " epochs:";

	  if ( ! no_hjorth ) logger << " ACT:" << cnt_act 
				    << " MOB:" << cnt_mob 
				    << " CMP:" << cnt_cmp ;
	  if ( calc_rms ) logger << " RMS:" << cnt_rms ;
	  if ( calc_clipped ) logger << " CLP:" << cnt_clp;
	  if ( calc_flat ) logger << " FLT:" << cnt_flt;
	  if ( calc_maxxed ) logger << " MAX:" << cnt_max;
	  	  
	  logger << "\n";
	  
	  if ( calc_rms ) writer.value( "CNT_RMS" , cnt_rms , "Epochs failing RMS filter" );
	  if ( calc_clipped) writer.value( "CNT_CLP" , cnt_clp , "Epochs failing CLIP filter" );
	  if ( calc_flat) writer.value( "CNT_FLT" , cnt_flt , "Epochs failing FLAT filter" );
	  if ( calc_maxxed) writer.value( "CNT_MAX" , cnt_max , "Epochs failing MAX filter" );

	  if ( ! no_hjorth ) {
	    writer.value( "CNT_ACT" , cnt_act , "Epochs failing H1 filter" );
	    writer.value( "CNT_MOB" , cnt_mob , "Epochs failing H2 filter" );
	    writer.value( "CNT_CMP" , cnt_cmp , "Epochs failing H3 filter" );
	  }
	  
	  writer.value( "FLAGGED_EPOCHS" , total,    "Number of epochs failing SIGSTATS" );
	  writer.value( "ALTERED_EPOCHS" , altered , "Number of epochs actually masked, i.e. not already masked" );
	  writer.value( "TOTAL_EPOCHS"   , ne,       "Number of epochs tested" );
	
	} // next signal      
      
      writer.unlevel( globals::signal_strat );

    }
  

  //
  // Phase 3: within each epoch, find outlier channels, and track
  //  i.e. if we have multiple, comparable channels, which are consisent outliers?
  //       either calculate for all epochs,
  //           or just for unmasked epochs (i.e. if th=X and cstats-unmasked-only)
  //
  
  if ( cstats && ns > 2 )
    {

      logger << "  calculating between-channel statistics, ";

      if ( cstats_all ) logger << "based on all epochs\n";
      else logger << "based only on unmasked epochs\n";

      logger << "  threshold (for P_H1, P_H2, P_H3) is " << ch_th << " SD units\n";
      
      // mean over epochs (Z score of this channel versus all others)
      std::vector<double> m_ch_h1(ns), m_ch_h2(ns), m_ch_h3(ns);

      // number/proportion of epochs where channel has |Z| > ch_th; (or any H, 'out')
      std::vector<double> t_ch_h1(ns), t_ch_h2(ns), t_ch_h3(ns), t_ch_out(ns);

      int ne_actual = 0;
      
      for (int ei=0; ei<ne; ei++)
	{
	  //	  std::cerr <<"e = " << ei << "\n";

	  // only consider unmasked epochs here?
	  if ( ! cstats_all )
	    if ( edf.timeline.masked( e_epoch[si][ei] ) ) continue;
	  
	  //	  std::cerr << "considering epoch " << ei << "\n";
	  
	  std::vector<double> tmp_h1(ns), tmp_h2(ns), tmp_h3(ns);

	  for (int si=0;si<ns;si++)
	    {
	      tmp_h1[si] = e_act[si][ei];
	      tmp_h2[si] = e_mob[si][ei];
	      tmp_h3[si] = e_cmp[si][ei];	
	    }

	  // normalize
	  tmp_h1 = MiscMath::Z( tmp_h1 );
	  tmp_h2 = MiscMath::Z( tmp_h2 );
	  tmp_h3 = MiscMath::Z( tmp_h3 );
	
	  // accumulate
	  for (int si=0;si<ns;si++)
	    {
	      tmp_h1[si] = fabs( tmp_h1[si] );
	      tmp_h2[si] = fabs( tmp_h2[si] );
	      tmp_h3[si] = fabs( tmp_h3[si] );
	      
	      if ( tmp_h1[si] > ch_th ) ++t_ch_h1[si];
	      if ( tmp_h2[si] > ch_th ) ++t_ch_h2[si];
	      if ( tmp_h3[si] > ch_th ) ++t_ch_h3[si];

	      // any metric above threshold?
	      if ( tmp_h1[si] > ch_th | tmp_h2[si] > ch_th | tmp_h3[si] > ch_th )
		++t_ch_out[si];

	      m_ch_h1[si] += tmp_h1[si];
	      m_ch_h2[si] += tmp_h2[si];
	      m_ch_h3[si] += tmp_h3[si];
	      
	    }

	  // track epochs included in analysis
	  
	  ++ne_actual;

	  // next epoch
	}

      // normalize by number of epochs

      
      for (int si=0;si<ns;si++)
	{
	  m_ch_h1[si] /= (double)ne_actual;
	  m_ch_h2[si] /= (double)ne_actual;
	  m_ch_h3[si] /= (double)ne_actual;
	  
	  t_ch_h1[si] /= (double)ne_actual;
	  t_ch_h2[si] /= (double)ne_actual;
	  t_ch_h3[si] /= (double)ne_actual;
	  t_ch_out[si] /= (double)ne_actual;

	  writer.level( signals.label( sdata[si] ) , globals::signal_strat );

	  writer.value( "Z_H1" , m_ch_h1[si] );
	  writer.value( "Z_H2" , m_ch_h2[si] );
	  writer.value( "Z_H3" , m_ch_h3[si] );
	  
	  writer.value( "P_H1" , t_ch_h1[si] );
	  writer.value( "P_H2" , t_ch_h2[si] );
	  writer.value( "P_H3" , t_ch_h3[si] );
	  writer.value( "P_OUT" , t_ch_out[si] );
	  
	}
      
      writer.unlevel( globals::signal_strat );

    }
      
  

  //
  // Turning rate sub-epoch level reporting (including smoothing over sub-epochs)
  //

  if ( turning_rate )
    {
      Helper::halt( "turning-rate not implemented" );
      Helper::halt( "need to fix signal indexing, ns vs ns_all, si, as above");
      // i.e. to skip annotation channels
      
      for (int s=0;s<ns;s++)
	{
	  int sr = edf.header.sampling_freq( s );
	  
	  // how many units (in # of sub-epoch units);  +1 means includes self
	  int winsize = 1 + tr_epoch_smooth / tr_epoch_sec ; 

	  logger << "sz = " << e_tr[s].size() << " " << winsize << "\n";
	  e_tr[s] = MiscMath::moving_average( e_tr[s] , winsize );

	  // output
	  writer.level( signals.label(s) , globals::signal_strat );
	  for (int i=0;i<e_tr[s].size();i++)
	    {
	      writer.level( i+1 , "SUBEPOCH" );
	      writer.value( "TR" , e_tr[s][i] );
	    }
	  writer.unlevel( "SUBEPOCH" );
	}
      writer.unlevel( globals::signal_strat );
    }



  //
  // Phase 4: astatst (compare epoch to ALL epochs and ALL channels, i.e. th + cstats )
  //

  if ( astats )
    {

      logger << "  setting CHEP mask based on astats\n";

      //
      // Temporary 'chep' mask
      //

      std::vector<std::vector<bool> > emask(ns);
      for (int si=0;si<ns;si++)
	{
	  emask[si].resize(ne,false);
	  for (int ei=0; ei<ne; ei++)
	    if ( edf.timeline.masked( e_epoch[si][ei] , signals(sdata[si]) ) )
	      emask[si][ei] = true;		 
	}
      
      //
      // Iterative outlier removal
      //

      
      for (int t = 0 ; t < astats_th.size(); t++)
	{
	  
	  // standardize over all epochs and channels
	  double h1_mean = 0 , h2_mean = 0 , h3_mean = 0;
	  double h1_sd = 0 , h2_sd = 0 , h3_sd = 0;
	  uint64_t cnt = 0;
	  
	  for (int si=0;si<ns;si++)
	    for (int ei=0; ei<ne; ei++)
	      if ( ! emask[si][ei] ) 
		{
		  h1_mean += e_act[si][ei];
		  h2_mean += e_mob[si][ei];
		  h3_mean += e_cmp[si][ei];
		  ++cnt;
		}

	  h1_mean /= (double)cnt;
	  h2_mean /= (double)cnt;
	  h3_mean /= (double)cnt;
	  
	  for (int si=0;si<ns;si++)
	    for (int ei=0; ei<ne; ei++)
	      if ( ! emask[si][ei] ) 
		{
		  h1_sd += ( e_act[si][ei] - h1_mean ) * ( e_act[si][ei] - h1_mean ) ;
		  h2_sd += ( e_mob[si][ei] - h2_mean ) * ( e_mob[si][ei] - h2_mean ) ;
		  h3_sd += ( e_cmp[si][ei] - h3_mean ) * ( e_cmp[si][ei] - h3_mean ) ;
		}
	  
	  h1_sd = sqrt( h1_sd / (double)(cnt-1) );
	  h2_sd = sqrt( h2_sd / (double)(cnt-1) );
	  h3_sd = sqrt( h3_sd / (double)(cnt-1) );

	  double h1_lwr = h1_mean - astats_th[t] * h1_sd;
	  double h2_lwr = h2_mean - astats_th[t] * h2_sd;
	  double h3_lwr = h3_mean - astats_th[t] * h3_sd;
			    
	  double h1_upr = h1_mean + astats_th[t] * h1_sd;
	  double h2_upr = h2_mean + astats_th[t] * h2_sd;
	  double h3_upr = h3_mean + astats_th[t] * h3_sd;
	  
	  // mask
	  uint64_t masked = 0;
	  for (int si=0;si<ns;si++)
            for (int ei=0; ei<ne; ei++)
	      if ( ! emask[si][ei] )
		{
		  if      ( e_act[si][ei] < h1_lwr || e_act[si][ei] > h1_upr ) { emask[si][ei] = true; ++masked; } 
		  else if ( e_mob[si][ei] < h2_lwr || e_mob[si][ei] > h2_upr ) { emask[si][ei] = true; ++masked; }
		  else if ( e_cmp[si][ei] < h3_lwr || e_cmp[si][ei] > h3_upr ) { emask[si][ei] = true; ++masked; }
		}

	  // all done for this iteration
	  logger << "  masked " << masked << " CHEPs of " << cnt << " unmasked CHEPs ("
		 << 100*(masked/(double(ns*ne))) << "%), from " << ne*ns << " total  CHEPs, "
		 << "on iteration " << t+1 << "\n";

	}

      //
      // fill in CHEP mask
      //
      
      for (int si=0;si<ns;si++)
	for (int ei=0; ei<ne; ei++)
	  if (  emask[si][ei] )
	    edf.timeline.set_chep_mask( e_epoch[si][ei] , signals(sdata[si]) );


      //
      // end of 'astats' 
      //
    }

  
  
  //
  // Individual level summary
  //
  
  for (int si=0;si<ns;si++)
    {
      writer.level( signals.label(sdata[si]) , globals::signal_strat );
      
      writer.value( "H1"   , mean_activity[si] / (double)n[si] , "Mean H1 statistic" );
      writer.value( "H2"   , mean_mobility[si] / (double)n[si] , "Mean H2 statistic" );
      writer.value( "H3"   , mean_complexity[si] / (double)n[si] , "Mean H3 statistic" );

      if ( calc_clipped )
	writer.value( "CLIP" , clipped[si] / (double)n[si] , "Mean CLIP statistic" );

      if ( calc_flat )
	writer.value( "FLAT" , flat[si] / (double)n[si] , "Mean FLAT statistic" );

      if ( calc_maxxed )
	writer.value( "MAX" , maxxed[si] / (double)n[si] , "Mean MAX statistic" );

      if ( calc_rms )
	writer.value( "RMS"   , rms[si] / (double)n[si] , "Mean RMS statistic" );      
    }

  writer.unlevel( globals::signal_strat );

}




void  mse_per_epoch( edf_t & edf , param_t & param )
{
  

  //
  // Calculate MSE per epoch and average
  //

  //
  // MSE parameters
  //

  int    m  = param.has("m") ? param.requires_int("m") : 2;  

  double r  = param.has("r") ? param.requires_dbl("r") : 0.15;
  
  std::vector<int> scale;
  if ( param.has("s") ) 
    {
      scale = param.intvector("s");
      if ( scale.size() != 3 ) Helper::halt( "mse s=lwr,upr,inc" );
    }
  else
    {
      scale.push_back(1);
      scale.push_back(10);
      scale.push_back(2);
    }

  //
  // Output
  //
    
  bool verbose = param.has( "verbose" ) ;

  
  //
  // Attach signal(s)
  //

  std::string signal_label = param.requires( "sig" );  
  
  signal_list_t signals = edf.header.signal_list( signal_label );  

  const int ns = signals.size();

  
  
  //
  // For each signal  
  //
  
  for (int s=0;s<ns;s++)
    {

      //
      // only consider data tracks
      //
      
      if ( edf.header.is_annotation_channel( signals(s) ) ) continue;


      logger << " estimating MSE for " << signals.label(s) << "\n";
      
      //
      // output stratifier
      //
      
      writer.level( signals.label(s) , globals::signal_strat );	  
      
      //
      // to track overall mean over epochs: scale -> vector of per-epoch MSEs
      //
      
      std::map<int,std::vector<double> > all_mses;


      //
      // Point to first epoch (assume 30 seconds, but could be different)
      //
      
      int ne = edf.timeline.first_epoch();
      
      if ( ne == 0 ) return;
      

      //
      // for each each epoch 
      //

      while ( 1 ) 
	{
	  
	  //
	  // Get next epoch
	  //
	  
	  int epoch = edf.timeline.next_epoch();
	  
	  if ( epoch == -1 ) break;
	  
	  interval_t interval = edf.timeline.epoch( epoch );
	  	  
	  //
	  // get data
	  //

	  slice_t slice( edf , signals(s) , interval );
	  
	  std::vector<double> * d = slice.nonconst_pdata();

	  
	  //
	  // Mean-centre 30-second window, calculate RMS
	  //

	  mse_t mse( scale[0] , scale[1] , scale[2] , m , r );
	  
	  std::map<int,double> mses = mse.calc( *d );
	  
	  //
	  // track
	  //
	  

	  if ( verbose )
	    writer.epoch( edf.timeline.display_epoch( epoch ) );
	  
	  std::map<int,double>::const_iterator ii = mses.begin();
	  while ( ii != mses.end() )
	    {
	      const int & scale = ii->first ; 
	      
	      all_mses[  scale ].push_back( ii->second ); 
	      
	      // verbose output?

	      if ( verbose )
		{
		  writer.level( scale , "SCALE" );
		  writer.value( "MSE" , ii->second );      	  
		}
	      
	      // next scale
	      ++ii;
	    }
	  
	  if ( verbose )
	    writer.unlevel( "SCALE" );
	      
	} // next epoch
  
      if ( verbose ) 
	writer.unepoch();
      
      
      // overall means
      
      std::map<int,std::vector<double> >::const_iterator ii = all_mses.begin();
      while ( ii != all_mses.end() )
	{
	  double mse = 0;
	  const int & scale = ii->first;
	  const std::vector<double> & x = ii->second;
	  for (int i=0;i<x.size();i++) mse += x[i];
	  mse /= (double)x.size();
	  
	  writer.level( scale , "SCALE" );
	  writer.value( "MSE" , mse );
	  
	  ++ii;
	}
      writer.unlevel( "SCALE" );
      
    } // next signal
  
  writer.unlevel( globals::signal_strat );
  
}




void lzw_per_epoch( edf_t & edf , param_t & param )
{
  
  //
  // Calculate LZW per epoch and average
  //

  //
  // LZW parameters
  //

  int nbins   = param.has( "nbins" ) ? param.requires_int( "nbins" ) : 20 ;
  int nsmooth = param.has( "nsmooth" ) ? param.requires_int( "nsmooth" ) : 1 ;

  bool epoched = param.has( "epoch" );

     
  //
  // Attach signal(s)
  //

  std::string signal_label = param.requires( "sig" );  
  
  signal_list_t signals = edf.header.signal_list( signal_label );  

  const int ns = signals.size();


  //
  // Point to first epoch (assume 30 seconds, but could be different)
  //
  
  int ne = edf.timeline.first_epoch();

  if ( ne == 0 ) return;
  
  
  //
  // For each signal  
  //
  
  for (int s=0;s<ns;s++)
    {
      
      //
      // only consider data tracks
      //
      
      if ( edf.header.is_annotation_channel( signals(s) ) ) continue;


      //
      // output stratifier
      //
      
      writer.level( signals.label(s) , globals::signal_strat );	  
      

      //
      // per-epoch, or whole-signal calculation?
      //
      
      if ( ! epoched ) 
	{

	  // get all data
	  interval_t interval = edf.timeline.wholetrace();
	  
	  slice_t slice( edf , s , interval );
	  const std::vector<double> * d = slice.pdata();
	  const int sz = d->size();

	  // designed for per-epoch data, but just use first 
	  // slot for entire signal
	  std::vector<std::vector<double> > track_lzw;
	  track_lzw.push_back( *d );
	  	  
	  // coarse-grain signal
	  coarse_t c( track_lzw , nbins , nsmooth );
	  
	  // compress	  
	  lzw_t lzw( c );
      
	  // index	  
	  double index = lzw.size(0) / (double)track_lzw[0].size();

	  // output
	  writer.value( "LZW" , index );
	  	  
	}
     

      //
      // Epoch level analyses
      //
     
      if ( epoched )
	{
	  
	  
	  int ne = edf.timeline.first_epoch();
	  
	  if ( ne == 0 ) return;

	  // track all signals here

	  std::vector<std::vector<double> > track_lzw;
	  std::vector<int> track_e;

	  //
	  // for each each epoch 
	  //
	  
	  while ( 1 ) 
	    {
	      
	      // Get next epoch
	  
	      int epoch = edf.timeline.next_epoch();
	  
	      if ( epoch == -1 ) break;
	  
	      interval_t interval = edf.timeline.epoch( epoch );
	      
	      // get data
	      
	      slice_t slice( edf , signals(s) , interval );
	      
	      std::vector<double> * d = slice.nonconst_pdata();
	      
	      // lzw_t class is designed for per-epoch data to be taken 
	      // all in one structure	      
	      track_lzw.push_back( *d );
	      track_e.push_back( epoch );
	      
	    } // next epoch	      
	      	      
	  // coarse-grain signal
	  coarse_t c( track_lzw , nbins , nsmooth );
	  
	  // compress	  
	  lzw_t lzw( c );
      
	  // index	  
	  for (int e=0; e<track_e.size(); e++)
	    {

	      double index = lzw.size(e) / (double)track_lzw[e].size();
	      
	      // output
	      writer.epoch( edf.timeline.display_epoch( track_e[e] ) );
	      writer.value( "LZW" , index );
	      writer.unepoch();
			     
	    }
	  
	}
      
      writer.unlevel( globals::signal_strat );
    } // next signal
  
}



void    spike_signal( edf_t & edf , int s1 , int s2 , double wgt , const std::string & ns )
{

  if ( s1 == s2 ) return;
  if ( edf.header.is_annotation_channel(s1) ) Helper::halt( "annotation channel specified for SPIKE" ) ;
  if ( edf.header.is_annotation_channel(s2) ) Helper::halt( "annotation channel specified for SPIKE" ) ;

  bool append_new_channel = ns != "";
  
  interval_t interval = edf.timeline.wholetrace();
  
  // Right now, need a similar sampling rate
  
  const int Fs1 = edf.header.sampling_freq( s1 );
  const int Fs2 = edf.header.sampling_freq( s2 );
  
  const std::string label1 = edf.header.label[s1];
  const std::string label2 = edf.header.label[s2];

  if ( Fs1 != Fs2 ) 
  {
    logger << "Note: resampling " << label2 << " to " << Fs1 << " to match " << label1 << "\n";
    dsptools::resample_channel( edf, s2 , Fs1 );
  }

  slice_t slice1( edf , s1 , interval );
  const std::vector<double> * d1 = slice1.pdata();
  const int sz1 = d1->size();
 
  slice_t slice2( edf , s2 , interval );
  const std::vector<double> * d2 = slice2.pdata();
  const int sz2 = d2->size();
  
  if ( sz1 != sz2 ) Helper::halt( "problem in SPIKE, unequal channel lengths" );
      
  // apply SPIKE
  std::vector<double> spiked( sz1 , 0);
  for (int i=0;i<sz1;i++)
    spiked[i] = (*d1)[i] + wgt * (*d2)[i];
  
  
  // either UPDATE
  if ( append_new_channel )
    {
      const std::string label = edf.header.label[s1] + "-spike-" + edf.header.label[s1] + "-wgt-" + Helper::dbl2str( wgt );      
      edf.add_signal( label , Fs1 , spiked ); 
    }
  else // ... else UPDATE
    edf.update_signal( s1 , &spiked );

}
