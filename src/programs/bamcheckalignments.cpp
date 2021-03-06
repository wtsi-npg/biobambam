#include <iostream>
#include <cstdlib>
#include <libmaus/util/ArgInfo.hpp>
#include <libmaus/util/OutputFileNameTools.hpp>
#include <libmaus/util/GetFileSize.hpp>
#include <libmaus/fastx/FastAReader.hpp>
#include <libmaus/util/TempFileRemovalContainer.hpp>
#include <libmaus/bambam/BamDecoder.hpp>
#include <libmaus/bambam/BamWriter.hpp>

bool checkCigarValid(
	::libmaus::bambam::BamAlignment const & alignment,
	::libmaus::bambam::BamHeader const & bamheader,
	::libmaus::autoarray::AutoArray < ::libmaus::autoarray::AutoArray<uint8_t>::unique_ptr_type > const & text
)
{
	if ( alignment.isUnmap() )
		return true;

	if ( ! alignment.isCigarLengthConsistent() )
	{
		std::cerr << "[E] inconsistent cigar " << alignment.getCigarString() << " for " << alignment.getName() << std::endl;
		return false;
	}
	
	if ( alignment.getRefID() < 0 || alignment.getRefID() >= static_cast<int64_t>(bamheader.chromosomes.size()) )
	{
		std::cerr << "[E] reference id " << alignment.getRefID() << " out of range for " << alignment.getName() << std::endl;
		return false;
	}
	
	::libmaus::autoarray::AutoArray<uint8_t> const & ctext = *(text[alignment.getRefID()]);
	int64_t refpos = alignment.getPos();
	int64_t seqpos = 0;
	bool alok = true;
	std::string const read = alignment.getRead();
	
	for ( uint64_t i = 0; alok && i < alignment.getNCigar(); ++i )
	{
		char const cop = alignment.getCigarFieldOpAsChar(i);
		int64_t const clen = alignment.getCigarFieldLength(i);
		
		switch ( cop )
		{
			// match/mismatch, increment both
			case '=':
			case 'X':
			case 'M':
			{
				for ( int64_t j = 0; alok && j < clen; ++j, ++refpos, ++ seqpos )
				{
					if ( refpos < 0 || refpos >= static_cast<int64_t>(ctext.size()) )
					{
						std::cerr << "[E] " << cop << " operation outside of chromosome coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;
					}
					else if ( seqpos >= alignment.getLseq() )
					{
						std::cerr << "[E] " << cop << " operation outside of sequence coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;
					}
					else if ( cop == '=' && toupper(ctext[refpos]) != toupper(read[seqpos]) )
					{
						std::cerr << "[E] " << cop << " operation but mismatch between reference and query." << std::endl;
						alok = false;
					}
					else if ( cop == 'X' && toupper(ctext[refpos]) == toupper(read[seqpos]) )
					{
						std::cerr << "[E] " << cop << " operation but mismatch between reference and query." << std::endl;
						alok = false;
					}
				}
				break;
			}
			// insert into reference, increment seq
			case 'P':
			case 'I':
			{
				for ( int64_t j = 0; alok && j < clen; ++j, ++seqpos )
				{
					if ( seqpos >= alignment.getLseq() )
					{
						std::cerr << "[E] " << cop << " operation outside of sequence coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;						
					}
				}
				break;
			}
			// delete from reference, increment ref
			case 'D':
			{
				for ( int64_t j = 0; alok && j < clen; ++j, ++refpos )
				{
					if ( refpos < 0 || refpos >= static_cast<int64_t>(ctext.size()) )
					{
						std::cerr << "[E] " << cop << " operation outside of reference coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;						
					}
				}
				break;
			}
			// soft clipping, increment seq
			case 'S':
			{
				for ( int64_t j = 0; alok && j < clen; ++j, ++seqpos )
				{
					if ( seqpos >= alignment.getLseq() )
					{
						std::cerr << "[E] " << cop << " operation outside of sequence coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;						
					}
				}
				break;
			}
			// hard clipping, do nothing
			case 'H':
			{
				break;
			}
			// skip region in reference, increment ref
			case 'N':
			{
				for ( int64_t j = 0; alok && j < clen; ++j, ++refpos )
				{
					if ( refpos < 0 || refpos >= static_cast<int64_t>(ctext.size()) )
					{
						std::cerr << "[E] " << cop << " operation outside of reference coordinate range " << " for " << alignment.getName() << std::endl;
						alok = false;						
					}
				}
				break;
			}
		}
	}
	
	return alok;
}

int main(int argc, char * argv[])
{
	try
	{
		::libmaus::util::ArgInfo const arginfo(argc,argv);
		::libmaus::util::TempFileRemovalContainer::setup();
		
		::std::vector<std::string> const & inputfilenames = arginfo.restargs;
		char const * fasuffixes[] = { ".fa", ".fasta", 0 };
		std::string defoutname = libmaus::util::OutputFileNameTools::endClipLcp(inputfilenames,&fasuffixes[0]) + ".fa";
		while ( ::libmaus::util::GetFileSize::fileExists(defoutname) )
			defoutname += "_";
		std::string const fatempfilename = arginfo.getValue<std::string>("fatempfilename",defoutname);
		::libmaus::util::TempFileRemovalContainer::addTempFile(fatempfilename);
		
		// std::cerr << "output file name " << defoutname << std::endl;
		
		::std::vector< ::libmaus::fastx::FastAReader::RewriteInfo > const info = ::libmaus::fastx::FastAReader::rewriteFiles(inputfilenames,fatempfilename);
		
		std::map < std::string, uint64_t > fachr;
		::libmaus::autoarray::AutoArray < uint64_t > fapref(info.size()+1);
		for ( uint64_t i = 0; i < info.size(); ++i )
		{
			// std::cerr << info[i].valid << "\t" << info[i].idlen << "\t" << info[i].seqlen << "\t" << info[i].getIdPrefix() << std::endl;
			fachr[info[i].getIdPrefix()] = i;
			fapref [ i ] = info[i].getEntryLength() ;
		}
		fapref.prefixSums();
		for ( uint64_t i = 0; i < info.size(); ++i )
			fapref [ i ] += info[i].idlen + 2; // > + newline

		::libmaus::bambam::BamDecoder decoder(std::cin);
		::libmaus::bambam::BamHeader const & bamheader = decoder.bamheader;
		// std::vector< ::libmaus::bambam::Chromosome > chromosomes

		::libmaus::autoarray::AutoArray<uint8_t> uptab(256,false);
		for ( uint64_t j = 0; j < uptab.size(); ++j )
			uptab[j] = toupper(j);
		
		::libmaus::autoarray::AutoArray < ::libmaus::autoarray::AutoArray<uint8_t>::unique_ptr_type > text(bamheader.chromosomes.size());
		for ( uint64_t i = 0; i < bamheader.chromosomes.size(); ++i )
		{
			std::string const bamchrname = bamheader.chromosomes[i].name;
			if ( fachr.find(bamchrname) == fachr.end() )
			{
				::libmaus::exception::LibMausException se;
				se.getStream() << "Unable to find reference sequence " << bamchrname << " in fa file." << std::endl;
				se.finish();
				throw se;
			}
			uint64_t const faid = fachr.find(bamchrname)->second;
			if ( bamheader.chromosomes[i].len != info[faid].seqlen )
			{
				::libmaus::exception::LibMausException se;
				se.getStream() << "Reference sequence " << bamchrname << " has len " << bamheader.chromosomes[i].len << " in bam file but " << info[faid].seqlen << " in fa file." << std::endl;
				se.finish();
				throw se;
			}
			
			if ( bamheader.chromosomes.size() < 100 )
				std::cerr << "Loading sequence " << bamchrname << " of length " << info[faid].seqlen << std::endl;
			text [ i ] = UNIQUE_PTR_MOVE(::libmaus::autoarray::AutoArray<uint8_t>::unique_ptr_type(new ::libmaus::autoarray::AutoArray<uint8_t>(info[faid].seqlen,false)));
			::libmaus::aio::CheckedInputStream CIS(fatempfilename);
			CIS.seekg(fapref[faid]);
			CIS.read(reinterpret_cast<char *>(text[i]->begin()),info[faid].seqlen);
			// sanity check, next symbol in file should be a newline
			int c;
			c = CIS.get();
			assert ( c == '\n' );
			
			// convert to upper case
			for ( uint8_t * pa = text[i]->begin(); pa != text[i]->end(); ++pa )
				*pa = uptab[*pa];
		}
		
		for ( uint64_t i = 0; i < bamheader.chromosomes.size(); ++i )
		{
			assert ( text[i]->size() == bamheader.chromosomes[i].len );
		}
		
		uint64_t decoded = 0;
		
		::libmaus::bambam::BamWriter BW(std::cout,bamheader);
		
		while ( decoder.readAlignment() )
		{
			++decoded;
			
			if ( decoded % (1024*1024) == 0 )
			{
				std::cerr << "[V] " << decoded << std::endl;
			}
			
			::libmaus::bambam::BamAlignment & alignment = decoder.alignment;

			bool const cigok = checkCigarValid(alignment,bamheader,text);
			
			// if cigar is ok then keep alignment
			if ( cigok )
			{
				if ( !alignment.isUnmap() )
				{
					uint64_t seqpos = 0;
					uint64_t refpos = alignment.getPos();
					std::string const read = alignment.getRead();
					std::string modseq = read;
					::libmaus::autoarray::AutoArray<uint8_t> const & ctext = *(text[alignment.getRefID()]);
					
					std::ostringstream newcigarstream;

					for ( uint64_t i = 0; i < alignment.getNCigar(); ++i )
					{
						char const cop = alignment.getCigarFieldOpAsChar(i);
						int64_t const clen = alignment.getCigarFieldLength(i);
						
						switch ( cop )
						{
							// match/mismatch, increment both
							case 'M':
							{
								int64_t low = 0;
								
								while ( low != clen )
								{
									int64_t high = low;
									
									while ( high != clen && ctext[refpos] == read[seqpos] )
									{
										modseq[seqpos] = '=';
										++refpos, ++seqpos, ++ high;
									}
									if ( high != low )
										newcigarstream << high-low << "=";
										
									low = high;

									while ( high != clen && ctext[refpos] != read[seqpos] )
										++refpos, ++seqpos, ++ high;
									if ( high != low )
										newcigarstream << high-low << "X";
										
									low = high;
								}						
								
								break;
							}
							case '=':
							{
								refpos += clen;
								for ( int64_t j = 0; j < clen; ++j, ++seqpos )
									modseq[seqpos] = '=';
								newcigarstream << clen << cop; 
								break;
							}
							case 'X':
							{
								refpos += clen;
								seqpos += clen;
								newcigarstream << clen << cop; 
								break;
							}
							case 'P':
							case 'I':
							{
								seqpos += clen;
								newcigarstream << clen << cop; 
								break;
							}
							case 'N':
							case 'D':
							{
								refpos += clen;
								newcigarstream << clen << cop; 
								break;
							}
							case 'S':
							{
								seqpos += clen;
								newcigarstream << clen << cop; 
								break;
							}
							case 'H':
							{
								newcigarstream << clen << cop; 
								break;
							}
						}
					}
					
					alignment.replaceCigarString(newcigarstream.str());
					alignment.replaceSequence(modseq);
				}

				alignment.serialise(BW.bgzfos);
			}			
		}
	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}
