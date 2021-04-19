// Automatic Water Dispenser
// https://github.com/StorageB/Water-Dispenser
//
// Instructions for logging data to Google Sheets
// https://github.com/StorageB/Google-Sheets-Logging
 

// Enter Spreadsheet ID here
var SS = SpreadsheetApp.openById('enter_spreadsheet_ID_here');

var sheet = SS.getSheetByName('Sheet1');        // creates sheet class for Sheet1
var sheet2 = SS.getSheetByName('Calculations'); // creates sheet class for Calculations sheet
var str = "";

function doPost(e) {

  var parsedData;
  var result = {};
  
  try { 
    parsedData = JSON.parse(e.postData.contents);
  } 
  catch(f){
    return ContentService.createTextOutput("Error in parsing request body: " + f.message);
  }
   
  if (parsedData !== undefined){
    var flag = parsedData.format; 
    if (flag === undefined){
      flag = 0;
    }
    
    var dataArr = parsedData.values.split(","); // creates an array of the values taken from Arduino code
    
    var date_now = Utilities.formatDate(new Date(), "CST", "yyyy/MM/dd"); // gets the current date
    var time_now = Utilities.formatDate(new Date(), "CST", "hh:mm a");    // gets the current time
    
    var value0 = dataArr [0]; // run_total variable from Arduino code
    
    
    // read and execute command from the "payload_base" string from Arduino code
    switch (parsedData.command) {
      
      case "insert_row":
                  
         var range = sheet.getRange("A2:D2");
         range.insertCells(SpreadsheetApp.Dimension.ROWS); // insert cells just above the existing data instead of inserting an entire row
         
         sheet.getRange('A2').setValue(date_now);  // publish current date into Sheet1 cell A2
         sheet2.getRange('B3').setValue(date_now); // publish current date into Calculations sheet cell B3
         sheet.getRange('B2').setValue(time_now);  // publish current time into Sheet1 cell B2 
         sheet.getRange('C2').setValue(value0);    // publish run_total to Sheet1 cell C2
         
         var ounces  = ((sheet.getRange('C2').getValue() ) * sheet2.getRange('B1').getValue() ) / 1000 * 128; // calculate how many ounces used based on the conversion factor (from Calculations sheet B1) and run time (from Sheet1 C2)
         sheet.getRange('D2').setValue(ounces);   // publish ounces used to Sheet1 cell D2
         
         //str = "Data published"; // string to return back to serial console
         SpreadsheetApp.flush();
         break;     
       
    }
    
    // return data to Arduino
    //return ContentService.createTextOutput(str);
    
  // return data to Arduino
  var return_json = {
    'gallons':          sheet2.getRange('B2').getValue(),  // total gallons used
    'conversion':       sheet2.getRange('B1').getValue(),  // conversion factor being used
    'target':           sheet2.getRange('B13').getValue(), // daily target in ounces
    'filter':           sheet2.getRange('B18').getValue(), // what gallon value to change the filter
    'a':                sheet2.getRange('B21').getValue(), // ounces to automatically dispense (function 1)
    'b':                sheet2.getRange('B22').getValue(), // ounces to automatically dispense (function 2)
    'c':                sheet2.getRange('B23').getValue(), // ounces to automatically dispense (function 3)
    'd':                sheet2.getRange('B24').getValue(), // ounces to automatically dispense (function 4)
    'e':                sheet2.getRange('B25').getValue(), // ounces to automatically dispense (function 5)
    'afterhours_start': sheet2.getRange('B28').getValue(), // afterhours start time (hour from 0 to 23 where 23 would be 11pm and 0 would be midnight)
    'afterhours_stop':  sheet2.getRange('B29').getValue()  // afterhours start time (hour from 0 to 23 where 23 would be 11pm and 0 would be midnight)
  }; 
  return ContentService.createTextOutput(JSON.stringify(return_json)).setMimeType(ContentService.MimeType.JSON); // convert json to a string and send back to Arduino
  //return ContentService.createTextOutput("some text");
    
  } // endif (parsedData !== undefined)
  
  else{
    return ContentService.createTextOutput("Error! Request body empty or in incorrect format.");
  }
    
}
