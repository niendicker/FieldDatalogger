
#include "postgresql.h"

#define _cache_time ((double)10/10E2) /* Elapsed time(s) between export data do postgres  */

_sqlCtx *sqlCtxInit(_sqlCtx *sqlCtx, _ln *deviceConfig){
  assert(deviceConfig);
  sqlCtx = (_sqlCtx*)calloc(sizeof(_sqlCtx), _byte_size_);
  assert(sqlCtx);
  pid_t pid = getpid();
  sqlCtx->pid = salloc(str_digits(pid)); 
  sprintf(sqlCtx->pid, "%d", pid);
  assert(sqlCtx->pid);
  sqlCtx->hostname = salloc_init(peekValue( deviceConfig, (char*)"pgsqlHost"));
  sqlCtx->port     = (uint16_t)strtol(peekValue(deviceConfig, (char*)"pgsqlPort"), NULL, 10);
  char *auth = salloc_init(peekValue(deviceConfig, (char*)"pgsqlAuth"));
  sqlCtx->auth = salloc(strlen(auth));
  sprintf(sqlCtx->auth, auth, '='); /* put '=' signal  */
  sqlCtx->user     = salloc_init(peekValue( deviceConfig, (char*)"pgsqlUser"));
  sqlCtx->database = salloc_init(peekValue( deviceConfig, (char*)"pgsqlDatabase"));
  sqlCtx->schema   = salloc_init(_mbpoll_schema_); /* Schema is defined on sql template */
  char *deviceID = salloc_init(peekValue(deviceConfig, (char*)"tag"));
  sqlCtx->table    = salloc(strlen(_mbpoll_table_) + _byte_size_ +strlen(deviceID));
  sprintf(sqlCtx->table, "%s_%s", deviceID, _mbpoll_table_); /* deviceID_modbuspoll */
  /* Template SQL script to be executed agaist postgres */
  sqlCtx->sqlTemplate = (char*)salloc(strlen(_sql_template_copy_) + strlen(_mbpoll_sqlDir_));
  sprintf(sqlCtx->sqlTemplate, "%s%s", _mbpoll_sqlDir_, _sql_template_copy_);
  /* File used to export data */
  sqlCtx->inoutFile.persist_dt = _cache_time;
  sqlCtx->inoutFile.fileName = salloc(strlen(sqlCtx->pid) + _byte_size_ + strlen(_csv_file_));
  sprintf(sqlCtx->inoutFile.fileName, "%s_%s", sqlCtx->pid, _csv_file_); /* 12345678_mbpoll.csv */
  sqlCtx->inoutFile.filePath = salloc(strlen(_mbpoll_dataDir_) + strlen(sqlCtx->inoutFile.fileName));
  sprintf(sqlCtx->inoutFile.filePath, "%s%s", _mbpoll_dataDir_, sqlCtx->inoutFile.fileName); 
  free(deviceID);
  free(auth);
  return (sqlCtx != NULL ? sqlCtx : NULL);
};

int sqlCtxFree(_sqlCtx *sqlCtx){
  assert(sqlCtx);
  free(sqlCtx->sqlTemplate);
  free(sqlCtx->table);
  free(sqlCtx->schema);
  free(sqlCtx->database);
  free(sqlCtx->user);
  free(sqlCtx->auth);
  free(sqlCtx->hostname); 
  free(sqlCtx->inoutFile.filePath);  
  free(sqlCtx);  
  return 0;
};

/**
 * @brief  Generate timestamp with time zone. 'YYYY-MM-DD HH:MM:SS~TZ'
**/
char *timestampz(){
  time_t now = time(&now);
  assert(now);
  struct tm *ptm = localtime(&now);
  assert(ptm);
  int tsz_length = _timestampz_size; 
  char *tsz = salloc(tsz_length);
  strftime(tsz, tsz_length, "%F %T-03", ptm );
  assert(tsz);
  return (tsz != NULL ? tsz : NULL);
};

/**
 * @brief  Load, parse and Execute the sqlTemplate file against postgres using psql interface
**/
int runSql(_sqlCtx *ctx){
  assert(ctx);
  FILE *templateFile = fopen(ctx->sqlTemplate, "r");
  assert(templateFile);
  char *templateQuery = salloc(_sql_template_line_); /* Initialize vector */
  char *line = salloc(_sql_template_line_);
  while( fgets(line, _sql_template_line_, templateFile) ){ /* Load sql template file */
    int templateNewSize = strlen(templateQuery) + strlen(line);
    templateQuery = srealloc(templateQuery, templateNewSize);
    strcat(templateQuery, line);
  };
  fclose(templateFile);
  char *csvFile = ctx->inoutFile.filePath; 
  assert(csvFile);

  uint querySize = strlen(templateQuery) + strlen(ctx->table) + strlen(csvFile);
  int tagSize = 2; 
  querySize -= tagSize * 2; /* The 2 tags %s will be substituted by values TODO: */
  char *query = salloc(querySize);
  sprintf(query, templateQuery, ctx->table, csvFile); /* Set table/file for COPY query */
  char *auth = salloc_init(ctx->auth);
#ifndef QUIET_OUTPUT  
  char *psql = (char*)"psql";
#else 
  char *psql = (char*)"psql -q";  /* Be quiet */
#endif
  char *user = salloc_init(ctx->user);
  char *hostname = salloc_init(ctx->hostname);
  char *port = salloc(str_digits(ctx->port));
  sprintf(port, "%d", ctx->port);
  char *database = salloc_init(ctx->database);
  char *cmdTemplate = (char*)"%s %s --single-transaction --host=%s --port=%s --dbname=%s --username=%s --command=%s ";
  uint cmdSize = strlen(auth) + strlen(psql) + strlen(hostname) + 
                 strlen(port) + strlen(database) + strlen(user) + 
                 strlen(query) + strlen(cmdTemplate);
  cmdSize -= tagSize * 7; /* sprintf remove tags */
  char *cmd = (char*)salloc(cmdSize);
  sprintf(/*Output  */ cmd, 
          /*Template*/ cmdTemplate, 
          /*Values  */ auth, psql, hostname, port, database, user, query);
  int s = system(cmd); /* run query */

  free(cmd);
  free(database);
  free(port);
  free(hostname);
  free(user);
  free(auth);
  free(query);
  free(line);
  free(templateQuery);
  return s;
};

char *insertCsvHeader(_ln *deviceData){
    _ln *data = deviceData;
    char *csvHeader = salloc_init(_csv_timestamp_header_); /* Default column for all devices */
    char *csvColumn = salloc_init(_csv_timestamp_header_);
    char *columnID = salloc_init(data->data->key);
    while(data){ 
      char colDelimiter = ( data->next == NULL ) ? '\n' : _csv_std_delimiter_; /* Last row data? */
      columnID = srealloc(columnID, strlen(data->data->key));
      strcpy(columnID, data->data->key);
      csvColumn = srealloc( csvColumn, strlen(columnID) + _csv_delimiter_size_);
      sprintf(csvColumn, "%s%c", columnID, colDelimiter);
      int headerNewSize = strlen(csvHeader) + strlen(csvColumn);
      csvHeader = srealloc( csvHeader, headerNewSize);
      strcat(csvHeader, csvColumn);
      data = data->next;  
    } 
    free(columnID);
    free(csvColumn);
    assert(csvHeader);
    return csvHeader;
};

char *appendCsvData(_ln *deviceData, char *row){
    /* Insert timestamp with timezone as first column */
  assert(deviceData && row);
  char *tmz = timestampz();
  int tmzSize = strlen(row) + strlen(tmz) + _csv_delimiter_size_;
  row = srealloc(row, tmzSize);
  char *column = salloc(tmzSize);
  sprintf(column, "%s%c", tmz, _csv_std_delimiter_); 
  strcat(row, column);
  assert(row);
  _ln *currData = deviceData;
  assert(currData);
  while(currData){ /* Insert device's data */
    char delimiter = ( currData->next == NULL ) ? '\n' : _csv_std_delimiter_; /* Last row data? */
    char *data = currData->data->value;   /* Data */
    assert(data);
    char *dataType = currData->data->next->value; 
    assert(dataType);
    int colSize = strlen(data) + _csv_delimiter_size_;
    char *dataTemp = salloc(_byte_size_); /* initialize */
    if(strcmp(dataType, _pgsql_varchar_) == 0){ 
      colSize+= _csv_quote_size_;
      row = srealloc(row, strlen(row) + colSize);
      dataTemp = srealloc( dataTemp, colSize);
      sprintf(dataTemp, "%c%s%c%c", _csv_squote, data, _csv_squote, _csv_std_delimiter_);
    }
    else{
      row = srealloc(row, strlen(row) + colSize);
      dataTemp = srealloc( dataTemp, colSize);
      sprintf(dataTemp, "%s%c", data, delimiter);
    }
    strcat(row, dataTemp);
    currData = currData->next;
    free(dataTemp);
  } /* Insert device's data */
  free(tmz);
  free(column);
  return (row != NULL ? row : NULL );
};

int persistData(_ln *deviceData, _ln *deviceConfig){
  assert(deviceData && deviceConfig);
  static double dTime = _start_;
  static char *dataBuffer;
  static _sqlCtx *sqlCtx;
  if(dTime == _start_){ /* Start bufferring device data */
    cpu_time(_start_);
    sqlCtx = sqlCtxInit(sqlCtx, deviceConfig);
    _ln *dataAvaliable = deviceData;
    char *csvHeader = insertCsvHeader(dataAvaliable);
    dataBuffer = salloc_init(csvHeader);
    free(csvHeader);
  } /* Start bufferring device data */
  dTime = cpu_time(_check_);
  if( dTime > sqlCtx->inoutFile.persist_dt ) { /* Dump/Store buffered device data */
    FILE *outputFile = fopen(sqlCtx->inoutFile.fileName, "w+");
    if(outputFile == NULL)
      return -1;
    int outFileWritten = fprintf(outputFile, "%s", dataBuffer); 
    fclose(outputFile);
    if (outFileWritten){
      runSql(sqlCtx);  /* import dumped data to postgres using psql */
#ifndef QUIET_OUTPUT
      printf("Info: Buffered data saved\n");
#endif
    } 
    remove(sqlCtx->inoutFile.fileName);
    sqlCtxFree(sqlCtx);
    free(dataBuffer);
    dTime = cpu_time(_start_);
    return 0;
  }  /* Dump/Store buffered device data */
  dataBuffer = appendCsvData(deviceData, dataBuffer);
  assert(dataBuffer);
  return 0;
};