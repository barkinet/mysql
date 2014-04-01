/* Copyright (c) 2003-2007 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA */


#include <ndb_global.h>
#include <ndb_opts.h>

#include <NdbOut.hpp>

#include <NdbApi.hpp>
#include <NdbMain.h>
#include <NDBT.hpp> 
#include <NdbSleep.h>
 
int scanReadRecords(Ndb*, 
		    const NdbDictionary::Table*, 
		    const NdbDictionary::Index*,
		    int parallel,
		    int lockType,
		    bool headers,
		    bool useHexFormat,
		    char delim,
		    bool orderby,
                    bool descending);

NDB_STD_OPTS_VARS;

static const char* _dbname = "TEST_DB";
static const char* _delimiter = "\t";
static int _header, _parallelism, _useHexFormat, _lock,
  _order, _descending;

const char *load_default_groups[]= { "mysql_cluster",0 };

static int _tup = 0;
static int _dumpDisk = 0;
static int use_rowid = 0;
static int nodata = 0;
static int use_gci = 0;

static struct my_option my_long_options[] =
{
  NDB_STD_OPTS("ndb_desc"),
  { "database", 'd', "Name of database table is in",
    (uchar**) &_dbname, (uchar**) &_dbname, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "parallelism", 'p', "parallelism",
    (uchar**) &_parallelism, (uchar**) &_parallelism, 0,
    GET_INT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "lock", 'l', "Read(0), Read-hold(1), Exclusive(2)",
    (uchar**) &_lock, (uchar**) &_lock, 0,
    GET_INT, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "order", 'o', "Sort resultset according to index",
    (uchar**) &_order, (uchar**) &_order, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "descending", 'z', "Sort descending (requires order flag)",
    (uchar**) &_descending, (uchar**) &_descending, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "header", 'h', "Print header",
    (uchar**) &_header, (uchar**) &_header, 0,
    GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0 }, 
  { "useHexFormat", 'x', "Output numbers in hexadecimal format",
    (uchar**) &_useHexFormat, (uchar**) &_useHexFormat, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "delimiter", 'D', "Column delimiter",
    (uchar**) &_delimiter, (uchar**) &_delimiter, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "disk", 256, "Dump disk ref",
    (uchar**) &_dumpDisk, (uchar**) &_dumpDisk, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "rowid", 256, "Dump rowid",
    (uchar**) &use_rowid, (uchar**) &use_rowid, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "gci", 256, "Dump gci",
    (uchar**) &use_gci, (uchar**) &use_gci, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "tupscan", 't', "Scan in tup order",
    (uchar**) &_tup, (uchar**) &_tup, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { "nodata", 256, "Dont print data",
    (uchar**) &nodata, (uchar**) &nodata, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 }, 
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};
static void usage()
{
#ifdef NOT_USED
  char desc[] = 
    "tabname\n"\
    "This program reads all records from one table in NDB Cluster\n"\
    "and print them to stdout.  This is performed using a scan read.\n"\
    "(It only print error messages if it encounters a permanent error.)\n"\
    "It can also be used to dump the content of a table to file \n"\
    "  ex: select_all --no-header --delimiter=';' T4 > T4.data\n";
#endif
  ndb_std_print_version();
  print_defaults(MYSQL_CONFIG_NAME,load_default_groups);
  puts("");
  my_print_help(my_long_options);
  my_print_variables(my_long_options);
}

int main(int argc, char** argv){
  NDB_INIT(argv[0]);
  load_defaults("my",load_default_groups,&argc,&argv);
  const char* _tabname;
  int ho_error;
#ifndef DBUG_OFF
  opt_debug= "d:t:O,/tmp/ndb_select_all.trace";
#endif
  if ((ho_error=handle_options(&argc, &argv, my_long_options,
			       ndb_std_get_one_option)))
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  if ((_tabname = argv[0]) == 0) {
    usage();
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  }

  Ndb_cluster_connection con(opt_connect_str);
  con.set_name("ndb_select_all");
  if(con.connect(12, 5, 1) != 0)
  {
    ndbout << "Unable to connect to management server." << endl;
    return NDBT_ProgramExit(NDBT_FAILED);
  }
  if (con.wait_until_ready(30,0) < 0)
  {
    ndbout << "Cluster nodes not ready in 30 seconds." << endl;
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  Ndb MyNdb(&con, _dbname );
  if(MyNdb.init() != 0){
    ERR(MyNdb.getNdbError());
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  // Check if table exists in db
  const NdbDictionary::Table* pTab = NDBT_Table::discoverTableFromDb(&MyNdb, _tabname);
  const NdbDictionary::Index * pIdx = 0;
  if(argc > 1){
    pIdx = MyNdb.getDictionary()->getIndex(argv[1], _tabname);
  }

  if(pTab == NULL){
    ndbout << " Table " << _tabname << " does not exist!" << endl;
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  }

  if(argc > 1 && pIdx == 0)
  {
    ndbout << " Index " << argv[1] << " does not exists" << endl;
  }
  
  if(_order && pIdx == NULL){
    ndbout << " Order flag given without an index" << endl;
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  }

  if (_descending && ! _order) {
    ndbout << " Descending flag given without order flag" << endl;
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  }

  if (scanReadRecords(&MyNdb, 
		      pTab, 
		      pIdx,
		      _parallelism, 
		      _lock,
		      _header, 
		      _useHexFormat, 
		      (char)*_delimiter, _order, _descending) != 0){
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  return NDBT_ProgramExit(NDBT_OK);

}

int scanReadRecords(Ndb* pNdb, 
		    const NdbDictionary::Table* pTab, 
		    const NdbDictionary::Index* pIdx,
		    int parallel,
		    int _lock,
		    bool headers,
		    bool useHexFormat,
		    char delimiter, bool order, bool descending){

  int                  retryAttempt = 0;
  const int            retryMax = 100;
  int                  check;
  NdbTransaction       *pTrans;
  NdbScanOperation	       *pOp;
  NdbIndexScanOperation * pIOp= 0;

  NDBT_ResultRow * row = new NDBT_ResultRow(*pTab, delimiter);

  while (true){

    if (retryAttempt >= retryMax){
      ndbout << "ERROR: has retried this operation " << retryAttempt 
	     << " times, failing!" << endl;
      return -1;
    }

    pTrans = pNdb->startTransaction();
    if (pTrans == NULL) {
      const NdbError err = pNdb->getNdbError();

      if (err.status == NdbError::TemporaryError){
	NdbSleep_MilliSleep(50);
	retryAttempt++;
	continue;
      }
      ERR(err);
      return -1;
    }

    
    pOp = (!pIdx) ? pTrans->getNdbScanOperation(pTab->getName()) : 
      pIOp=pTrans->getNdbIndexScanOperation(pIdx->getName(), pTab->getName());
    
    if (pOp == NULL) {
      ERR(pTrans->getNdbError());
      pNdb->closeTransaction(pTrans);
      return -1;
    }

    int rs;
    unsigned scan_flags = 0;
    if (_tup) scan_flags |= NdbScanOperation::SF_TupScan;
    switch(_lock + (3 * order)){
    case 1:
      rs = pOp->readTuples(NdbScanOperation::LM_Read, scan_flags, parallel);
      break;
    case 2:
      rs = pOp->readTuples(NdbScanOperation::LM_Exclusive, scan_flags, parallel);
      break;
    case 3:
      rs = pIOp->readTuples(NdbScanOperation::LM_CommittedRead, 0, parallel, 
			    true, descending);
      break;
    case 4:
      rs = pIOp->readTuples(NdbScanOperation::LM_Read, 0, parallel, true, descending);
      break;
    case 5:
      rs = pIOp->readTuples(NdbScanOperation::LM_Exclusive, 0, parallel, true, descending);
      break;
    case 0:
    default:
      rs = pOp->readTuples(NdbScanOperation::LM_CommittedRead, scan_flags, parallel);
      break;
    }
    if( rs != 0 ){
      ERR(pTrans->getNdbError());
      pNdb->closeTransaction(pTrans);
      return -1;
    }
    
    if(0){
      NdbScanFilter sf(pOp);
#if 0
      sf.begin(NdbScanFilter::AND);
      sf.le(0, (Uint32)10);
      
      sf.end();
#elif 0
      sf.begin(NdbScanFilter::OR);
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)10);
      sf.lt(0, (Uint32)20);
      sf.end();
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)30);
      sf.lt(0, (Uint32)40);
      sf.end();
      sf.end();
#elif 1
      sf.begin(NdbScanFilter::AND);
      sf.begin(NdbScanFilter::OR);
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)10);
      sf.lt(0, (Uint32)20);
      sf.end();
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)30);
      sf.lt(0, (Uint32)40);
      sf.end();
      sf.end();
      sf.begin(NdbScanFilter::OR);
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)0);
      sf.lt(0, (Uint32)50);
      sf.end();
      sf.begin(NdbScanFilter::AND);
      sf.ge(0, (Uint32)100);
      sf.lt(0, (Uint32)200);
      sf.end();
      sf.end();
      sf.end();
#endif
    } else {
      check = pOp->interpret_exit_ok();
      if( check == -1 ) {
	ERR(pTrans->getNdbError());
	pNdb->closeTransaction(pTrans);
	return -1;
      }
    }
    
    bool disk= false;
    for(int a = 0; a<pTab->getNoOfColumns(); a++)
    {
      const NdbDictionary::Column* col = pTab->getColumn(a);
      if(col->getStorageType() == NdbDictionary::Column::StorageTypeDisk)
	disk= true;

      if (!nodata)
	if((row->attributeStore(a) = pOp->getValue(col)) == 0)
	{
	  ERR(pTrans->getNdbError());
	  pNdb->closeTransaction(pTrans);
	  return -1;
	}
    }
    
    NdbRecAttr * disk_ref= 0;
    if(_dumpDisk && disk)
      disk_ref = pOp->getValue(NdbDictionary::Column::DISK_REF);

    NdbRecAttr * rowid= 0, *frag = 0, *gci = 0;
    if (use_rowid)
    {
      frag = pOp->getValue(NdbDictionary::Column::FRAGMENT);
      rowid = pOp->getValue(NdbDictionary::Column::ROWID);
    }

    if (use_gci)
    {
      gci = pOp->getValue(NdbDictionary::Column::ROW_GCI);
    }
    
    check = pTrans->execute(NdbTransaction::NoCommit);   
    if( check == -1 ) {
      const NdbError err = pTrans->getNdbError();
      
      if (err.status == NdbError::TemporaryError){
	pNdb->closeTransaction(pTrans);
	NdbSleep_MilliSleep(50);
	retryAttempt++;
	continue;
      }
      ERR(err);
      pNdb->closeTransaction(pTrans);
      return -1;
    }

    if (rowid)
      ndbout << "ROWID\t";
    
    if (gci)
      ndbout << "\tGCI";
    
    if (headers && !nodata)
      row->header(ndbout);
    
    if (disk_ref)
      ndbout << "\tDISK_REF";

    ndbout << endl;
    
    int eof;
    int rows = 0;
    eof = pOp->nextResult();
    
    while(eof == 0){
      rows++;

      if (useHexFormat)
	ndbout.setHexFormat(1);

      if (rowid)
      {
	ndbout << "[ fragment: " << frag->u_32_value()
	       << " m_page: " << rowid->u_32_value() 
	       << " m_page_idx: " << *(Uint32*)(rowid->aRef() + 4) << " ]";
	ndbout << "\t";
      }
      
      if (gci)
      {
	if (gci->isNULL())
	  ndbout << "NULL\t";
	else
	  ndbout << gci->u_64_value() << "\t";
      }

      if (!nodata)
	ndbout << (*row);
      
      if(disk_ref)
      {
	ndbout << "\t";
	ndbout << "[ m_file_no: " << *(Uint16*)(disk_ref->aRef()+6)
	       << " m_page: " << disk_ref->u_32_value() 
	       << " m_page_idx: " << *(Uint16*)(disk_ref->aRef() + 4) << " ]";
      }
      
      if (rowid || disk_ref || gci || !nodata)
	ndbout << endl;
      eof = pOp->nextResult();
    }
    if (eof == -1) {
      const NdbError err = pTrans->getNdbError();
      
      if (err.status == NdbError::TemporaryError){
	pNdb->closeTransaction(pTrans);
	NdbSleep_MilliSleep(50);
	retryAttempt++;
	continue;
      }
      ERR(err);
      pNdb->closeTransaction(pTrans);
      return -1;
    }
    
    pNdb->closeTransaction(pTrans);
    
    ndbout << rows << " rows returned" << endl;
    
    return 0;
  }
  return -1;
}
