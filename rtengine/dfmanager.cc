/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <dfmanager.h>
#include <options.h>
#include <giomm.h>
#include <guiutils.h>
#include <safegtk.h>
#include <common.h>
#include <sstream>
#include <stdio.h>

namespace rtengine{

extern Settings* settings;

// *********************** class dfInfo **************************************

inline dfInfo& dfInfo::operator =(const dfInfo &o){
	pathname = o.pathname;
	maker = o.maker;
	model = o.model;
	iso = o.iso;
	shutter = o.shutter;
	timestamp = o.timestamp;
	if( ri ){
		delete ri;
		ri = NULL;
	}
	return *this;
}

bool dfInfo::operator <(const dfInfo &e2) const
{
	if( this->maker.compare( e2.maker) >=0 )
		return false;
	if( this->model.compare( e2.model) >=0 )
		return false;
	if( this->iso >= e2.iso )
		return false;
	if( this->shutter >= e2.shutter )
		return false;
	if( this->timestamp >= e2.timestamp )
		return false;
	return true;
}

std::string dfInfo::key(const std::string &mak, const std::string &mod, int iso, double shut )
{
	std::ostringstream s;
	s << mak << " " << mod << " ";
	s.width(5);
	s << iso << "ISO ";
	s.precision( 2 );
	s.width(4);
	s << shut << "s";
	return s.str();
}

double dfInfo::distance(const std::string &mak, const std::string &mod, int iso, double shutter) const
{
	if( this->maker.compare( mak) != 0 )
		return INFINITY;
	if( this->model.compare( mod) != 0 )
		return INFINITY;
    double dISO= (log(this->iso/100.) - log(iso/100.))/log(2);
    double dShutter = (log(this->shutter) - log(shutter))/log(2);
    return sqrt( dISO*dISO +  dShutter*dShutter);
}

RawImage* dfInfo::getRawImage()
{
	if(ri)
		return ri;
	updateRawImage();
	updateBadPixelList( ri );

	return ri;
}

std::list<badPix>& dfInfo::getHotPixels()
{
	if( !ri ){
		updateRawImage();
		updateBadPixelList( ri );
	}
	return badPixels;
}
/* updateRawImage() load into ri the actual pixel data from pathname if there is a single shot
 * otherwise load each file from the pathNames list and extract a template from the media;
 * the first file is used also for reading all information other than pixels
 */
void dfInfo::updateRawImage()
{
	typedef unsigned int acc_t;
	if( pathNames.size() >0 ){
		std::list<Glib::ustring>::iterator iName = pathNames.begin();
		ri = new RawImage(*iName); // First file used also for extra pixels informations (width,height, shutter, filters etc.. )
		if( ri->loadRaw(true)){
			delete ri;
			ri=NULL;
		}else{

			acc_t **acc = new acc_t*[ri->height];
			for( int row=0; row<ri->height;row++)
				acc[row] = new acc_t[ri->width*(ri->filters?1:3)];

			// copy first image into accumulators
			for (int row = 0; row < ri->height; row++)
				for (int col = 0; col < ri->width * (ri->filters ? 1 : 3); col++)
					acc[row][col] = ri->data[row][col];
			int nFiles = 1; // First file data already loaded

			for( iName++; iName != pathNames.end(); iName++){
				RawImage* temp = new RawImage(*iName);
				if( !temp->loadRaw(true)){
					nFiles++;
					if( ri->filters ){
						for( int row=0; row<ri->height;row++){
							for( int col=0; col < ri->width;col++)
								acc[row][col] += temp->data[row][col];
						}
					}else{
						for( int row=0; row<ri->height;row++){
							for( int col=0; col < ri->width;col++){
								acc[row][3*col+0] += temp->data[row][3*col+0];
								acc[row][3*col+1] += temp->data[row][3*col+1];
								acc[row][3*col+2] += temp->data[row][3*col+2];
							}
						}
					}
				}
				delete temp;
			}
			for (int row = 0; row < ri->height; row++){
				for (int col = 0; col < ri->width * (ri->filters ? 1 : 3); col++)
					ri->data[row][col] = acc[row][col] / nFiles;
				delete [] acc[row];
			}
			delete [] acc;
		}
	}else{
		ri = new RawImage(pathname);
		if( ri->loadRaw(true)){
			delete ri;
			ri=NULL;
		}
	}
}

void dfInfo::updateBadPixelList( RawImage *df )
{
	const int threshold=10;
	if( df->filters ){
		for( int row=2; row<df->height-2; row++)
			for( int col=2; col < df->width-2; col++){
				int m =	(df->data[row-2][col-2] + df->data[row-2][col] + df->data[row-2][col+2]+
						df->data[row][col-2] + df->data[row][col+2]+
						df->data[row+2][col-2] + df->data[row+2][col] + df->data[row+2][col+2])/8;
				if( df->data[row][col]/threshold > m )
					badPixels.push_back( badPix(col,row) );
			}
	}else{
		for( int row=1; row<df->height-1; row++)
			for( int col=1; col < df->width-1; col++){
			   int m[3];
			   for( int c=0; c<3;c++){
				    m[c] =	(df->data[row-1][3*(col-1)+c] + df->data[row-1][3*col+c] + df->data[row-1][3*(col+1)+c]+
						     df->data[row]  [3*(col-1)+c] + df->data[row]  [3*col+c]+
						     df->data[row+1][3*(col-1)+c] + df->data[row+1][3*col+c] + df->data[row+1][3*(col+1)+c])/8;
			   }
			   if( df->data[row][3*col]/threshold > m[0] || df->data[row][3*col+1]/threshold > m[1] || df->data[row][3*col+2]/threshold > m[2])
					badPixels.push_back( badPix(col,row) );
			}
	}
	if( settings->verbose ){
		printf( "Extracted %u pixels from darkframe %s\n", badPixels.size(),df->fname.c_str() );
	}
}


// ************************* class DFManager *********************************

void DFManager::init( Glib::ustring pathname )
{
    std::vector<Glib::ustring> names;
    Glib::RefPtr<Gio::File> dir = Gio::File::create_for_path (pathname);
	if( dir && !dir->query_exists())
    	return;
	safe_build_file_list (dir, names, pathname);
	
	dfList.clear();
	bpList.clear();
    for (int i=0; i<names.size(); i++) {
        int lastdot = names[i].find_last_of ('.');
        if (lastdot != Glib::ustring::npos && names[i].substr(lastdot) == ".badpixels" ){
        	int n = scanBadPixelsFile( names[i] );
        	if( n>0 && settings->verbose)
        		printf("Loaded %s: %d pixels\n",names[i].c_str(),n);
        	continue;
        }
        try{
            addFileInfo(names[i]);
        }catch( std::exception e ){}
    }
    // Where multiple shots exist for same group, move filename to list
    for( dfList_t::iterator iter = dfList.begin(); iter != dfList.end();iter++ ){
    	dfInfo &i = iter->second;
    	if( i.pathNames.size()>0 && !i.pathname.empty() ){
    		i.pathNames.push_back( i.pathname );
    		i.pathname.clear();
    	}
    	if( settings->verbose ){
			if( !i.pathname.empty() )
			   printf( "%s:  %s\n",i.key().c_str(),i.pathname.c_str());
			else{
				printf( "%s: MEAN of \n    ",i.key().c_str());
				for( std::list<Glib::ustring>::iterator iter = i.pathNames.begin(); iter != i.pathNames.end();iter++  )
					printf( "%s, ", iter->c_str() );
				printf("\n");
			}
    	}
    }
    currentPath = pathname;
    return;
}

dfInfo *DFManager::addFileInfo(const Glib::ustring &filename )
{
	Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(filename);
    if (!file )
        return false;
    if( !file->query_exists())
    	return false;
    Glib::RefPtr<Gio::FileInfo> info = safe_query_file_info(file);
    if (info && info->get_file_type() != Gio::FILE_TYPE_DIRECTORY && (!info->is_hidden() || !options.fbShowHidden)) {
        int lastdot = info->get_name().find_last_of ('.');
        if (options.is_extention_enabled(lastdot!=Glib::ustring::npos ? info->get_name().substr (lastdot+1) : "")){
        	RawImage ri(filename);
        	int res = ri.loadRaw(false); // Read informations about shot
        	if( !res ){
         	   /* Files are added in the map, divided by same maker/model,ISO and shutter*/
        	   std::string key( dfInfo::key(ri.make, ri.model,(int)ri.iso_speed,ri.shutter) );
        	   dfList_t::iterator iter = dfList.find( key );
        	   if( iter == dfList.end() ){
				   dfInfo n(filename, ri.make, ri.model,(int)ri.iso_speed,ri.shutter,ri.timestamp);
				   iter = dfList.insert(std::pair< std::string,dfInfo>( key,n ) );
        	   }else{
        		   while( iter != dfList.end() && iter->second.key() == key && ABS(iter->second.timestamp - ri.timestamp) >60*60*6 ) // 6 hour difference
        			   iter++;

        		   if( iter != dfList.end() )
        		      iter->second.pathNames.push_back( filename );
        		   else{
    				   dfInfo n(filename, ri.make, ri.model,(int)ri.iso_speed,ri.shutter,ri.timestamp);
    				   iter = dfList.insert(std::pair< std::string,dfInfo>( key,n ) );
        		   }
        	   }
               return &(iter->second);
        	}
		}
    }
    return 0;
}

void DFManager::getStat( int &totFiles, int &totTemplates)
{
	totFiles=0;
	totTemplates=0;
    for( dfList_t::iterator iter = dfList.begin(); iter != dfList.end();iter++ ){
    	dfInfo &i = iter->second;
    	if( i.pathname.empty() ){
    		totTemplates++;
    		totFiles += i.pathNames.size();
    	}else
    		totFiles++;
    }
}

/*  The search for the best match is twofold:
 *  if perfect matches for iso and shutter are found, then the list is scanned for lesser distance in time
 *  otherwise if no match is found, the whole list is searched for lesser distance in iso and shutter
 */
dfInfo* DFManager::find( const std::string &mak, const std::string &mod, int isospeed, double shut, time_t t )
{
	if( dfList.size() == 0 )
		return 0;
	std::string key( dfInfo::key(mak,mod,isospeed,shut) );
	dfList_t::iterator iter = dfList.find( key );

	if(  iter != dfList.end() ){
		dfList_t::iterator bestMatch = iter;
		time_t bestDeltaTime = ABS(iter->second.timestamp - t);
		for(iter++; iter != dfList.end() && !key.compare( iter->second.key() ); iter++ ){
			time_t d = ABS(iter->second.timestamp - t );
			if( d< bestDeltaTime ){
				bestMatch = iter;
				bestDeltaTime = d;
			}
		}
        return &(bestMatch->second);
	}else{
		iter = dfList.begin();
		dfList_t::iterator bestMatch = iter;
		double bestD = iter->second.distance(  mak, mod, isospeed, shut );
		for( iter++; iter != dfList.end();iter++ ){
           double d = iter->second.distance(  mak, mod, isospeed, shut );
           if( d < bestD ){
        	   bestD = d;
        	   bestMatch = iter;
           }
		}
		return &(bestMatch->second);
	}
}

RawImage* DFManager::searchDarkFrame( const std::string &mak, const std::string &mod, int iso, double shut, time_t t )
{
   dfInfo *df = find( mak, mod, iso, shut, t );
   if( df )
      return df->getRawImage();
   else
	  return 0;
}

RawImage* DFManager::searchDarkFrame( const Glib::ustring filename )
{
	for ( dfList_t::iterator iter = dfList.begin(); iter != dfList.end();iter++ ){
		if( iter->second.pathname.compare( filename )==0  )
			return iter->second.getRawImage();
	}
	dfInfo *df = addFileInfo( filename );
	if(df)
		return df->getRawImage();
	return 0;
}
std::list<badPix> *DFManager::getHotPixels ( const Glib::ustring filename )
{
	for ( dfList_t::iterator iter = dfList.begin(); iter != dfList.end();iter++ ){
		if( iter->second.pathname.compare( filename )==0  )
			return &iter->second.getHotPixels();
	}
	return 0;
}
std::list<badPix> *DFManager::getHotPixels ( const std::string &mak, const std::string &mod, int iso, double shut, time_t t )
{
   dfInfo *df = find( mak, mod, iso, shut, t );
   if( df ){
	   if( settings->verbose )
		   if( df->pathname.size() >0 )
		      printf( "Searched hotpixels from %s\n",df->pathname.c_str());
		   else if( df->pathNames.size() >0 )
			  printf( "Searched hotpixels from template (first %s)\n",df->pathNames.begin()->c_str());
	   return &df->getHotPixels();
   }else
	   return 0;
}

int DFManager::scanBadPixelsFile( Glib::ustring filename )
{
	FILE *file = fopen( filename.c_str(),"r" );
	if( !file ) return false;
	int lastdot = filename.find_last_of ('.');
	int dirpos1 = filename.find_last_of ('/');
	int dirpos2 = filename.find_last_of ('\\');
	if( dirpos1 == Glib::ustring::npos && dirpos2== Glib::ustring::npos )
		dirpos1 =0;
	else
		dirpos1= (dirpos1> dirpos2 ? dirpos1: dirpos2);
	std::string makmodel(filename,dirpos1+1,lastdot-(dirpos1+1) );
	std::list<badPix> bp;
	char line[256];
    while( fgets(line,sizeof(line),file ) ){
    	int x,y;
    	if( sscanf(line,"%d %d",&x,&y) == 2 )
    		bp.push_back( badPix(x,y) );
    }
    int numPixels = bp.size();
    if( numPixels >0 )
    	bpList[ makmodel ] = bp;
	return numPixels;
}

std::list<badPix> *DFManager::getBadPixels ( const std::string &mak, const std::string &mod, const std::string &serial)
{
	std::ostringstream s;
	s << mak << " " <<mod;
	if( serial.size()>0)
	   s << " " << serial;
	bpList_t::iterator iter = bpList.find( s.str() );
	if( iter != bpList.end() ){
		if( settings->verbose )
		   printf("Found:%s.badpixels in list\n",s.str().c_str());
		return &(iter->second);
	}
	if( settings->verbose )
	   printf("%s.badpixels not found\n",s.str().c_str());
	return 0;
}

// Global variable
DFManager dfm;


}

