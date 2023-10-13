unit updiguimain;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls,
{$IFNDEF MSWINDOWS}
  {$IFNDEF NO_LIBC}
  Libc,
  KernelIoctl,
  {$ELSE}
  termio, baseunix, unix,
  {$ENDIF}
  {$IFNDEF FPC}
  Types,
  {$ENDIF}
{$ELSE}
  Windows, registry,
  {$IFDEF FPC}
  winver,
  {$ENDIF}
{$ENDIF}
  libUPDI,ctypes;

type

  { TForm1 }

  TForm1 = class(TForm)
    Button1: TButton;
    ComboBox1: TComboBox;
    ComboBox2: TComboBox;
    Label1: TLabel;
    Label2: TLabel;
    Log: TMemo;
    procedure Button1Click(Sender: TObject);
    procedure ComboBox1Change(Sender: TObject);
    procedure ComboBox1Select(Sender: TObject);
    procedure ComboBox2Select(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
  private
    loclogger : pUPDI_logger;
    cfg       : pUPDI_Params;
  public
    procedure LogOut(const Str : String);
  end;

var
  Form1: TForm1;

implementation

{$R *.lfm}


{ this part of code
  taken from     TLazSerial/lazsynaser.pas
  https://github.com/JurassicPork/TLazSerial
}
{$IFDEF MSWINDOWS}
function GetSerialPortNames: string;
var
  reg: TRegistry;
  l, v: TStringList;
  n: integer;
begin
  l := TStringList.Create;
  v := TStringList.Create;
  reg := TRegistry.Create;

  try
{$IFNDEF VER100}
{$IFNDEF VER120}
    reg.Access := KEY_READ;
{$ENDIF}
{$ENDIF}
    reg.RootKey := HKEY_LOCAL_MACHINE;
    reg.OpenKey('\HARDWARE\DEVICEMAP\SERIALCOMM\', false);
    reg.GetValueNames(l);
    for n := 0 to l.Count - 1 do
// Modif J.P  03/2013
      v.Add(Pchar(reg.ReadString(l[n])));
    Result := v.Text ;
  finally
    reg.Free;
    l.Free;
    v.Free;
  end;
end;
{$ENDIF}
{$IFNDEF MSWINDOWS}

{$ifndef DARWIN}
type
   TSerialStruct = packed record
          typ: cint;
          line: cint;
          port: cuint;
          irq:  cint;
          flags: cint;
          xmit_fifo_size: cint;
          custom_divisor: cint;
          baud_base: cint;
          close_delay: cushort;
          io_type: cchar;
          reserved_char:  pcchar;
          hub6: cint;
          closing_wait: cushort; // time to wait before closing
          closing_wait2: cushort; // no longer used...
          iomem_base: pcchar;
          iomem_reg_shift: cushort;
          port_high: clong; // cookie passed into ioremap
   end;
{$endif}

// Modif J.P   03/2013 - O1/2017 - 11/2022
function GetSerialPortNames: string;
var
  Index: Integer;
  Data: string;
  TmpPorts: String;
  flags : Longint;
  sr : TSearchRec;
// J.P  01/2017  new boolean parameter : special
  procedure ScanForPorts( const ThisRootStr : string; special :  boolean); // added by PDF
  var theDevice : String;
  var FD : Cint;
{$IFnDEF DARWIN}        // RPH - Added 14May2016
   var Ser : TSerialStruct;
{$ENDIF}
  begin
    if FindFirst( ThisRootStr, flags, sr) = 0 then
    begin
      repeat
        if (sr.Attr and flags) = Sr.Attr then
        begin
          data := sr.Name;
          index := length(data);
          theDevice := '/dev/' + data;
// try to open the device
       FD := fpopen(thedevice,O_RdWr or O_NonBlock or O_NoCtty);
       if FD > 0 then
          begin
// try to get serial info from the device
          {$IFDEF DARWIN}       // RPH - Added 14May2016 for OS-X
           if fpioctl( FD,TIOCEXCL, nil) <> -1  then
             begin
              TmpPorts := TmpPorts + sLineBreak + theDevice;
              fpclose(FD);
             end;
          {$ELSE}
           if fpioctl( FD,TIOCGSERIAL, @Ser) <> -1 then
             begin
// device is serial if type is not unknown (if not special device)
              // new parameter special
              if ((Ser.typ <> 0) OR (special) ) then
               TmpPorts := TmpPorts + sLineBreak + theDevice;
              fpclose(FD);
             end;
          {$ENDIF}
          end;
        end;
      until FindNext(sr) <> 0;
    FindClose(sr);
    end;
  end;

begin
  try
    TmpPorts := '';
    flags := faAnyFile AND (NOT faDirectory);
    ScanForPorts( '/dev/rfcomm*',true);
    ScanForPorts( '/dev/ttyUSB*',true);
    ScanForPorts( '/dev/ttyS*',false);
    ScanForPorts( '/dev/ttyACM*',true);
   {$IFDEF DARWIN}
    ScanForPorts( '/dev/tty.usbserial*',false); // RPH 14May2016, for FTDI driver
    ScanForPorts( '/dev/tty.UC-232*',false);    // RPH 15May2016, for Prolific driver
   {$ELSE}
     ScanForPorts( '/dev/ttyAM*',false); // for ARM board
   {$ENDIF}
  finally
    Result:=TmpPorts;
  end;
end;
{$ENDIF}

procedure log_onlog(ud: Pointer; level: int32; const src, msg: pansichar) cdecl;
begin
  TForm1(ud).LogOut(StrPas(src) + ': ' +StrPas(msg));
end;

{ TForm1 }

procedure TForm1.FormCreate(Sender: TObject);
var
  i, c : Integer;
  l : Integer;
  dst : AnsiString;
  ports : string;
begin
  loclogger := nil;
  cfg := nil;

  if InitUPDILibInterface('') then
  begin
    UPDILIB_set_glb_logger_onlog(@log_onlog, Self);

    loclogger := UPDILIB_logger_init(pansichar('gui'), LOG_LEVEL_INFO,
                                                       @log_onlog,
                                                       nil,
                                                       Self);

    SetLength(dst, COMPORT_LEN);
    cfg := UPDILIB_cfg_init();
    if Assigned(cfg) then
    begin
      UPDILIB_cfg_set_logger(cfg, loclogger);
      dst := 'dev/tty';
      UPDILIB_cfg_set_com(cfg, PChar(dst));
    end;

    SetLength(dst, DEVICES_NAME_LEN);
    c := UPDILIB_devices_get_count();
    ComboBox1.Items.BeginUpdate;
    try
      for i := 0 to c-1 do
      begin
        l := DEVICES_NAME_LEN;
        UPDILIB_devices_get_name(i, @(dst[1]), @l);
        ComboBox1.Items.Add(Copy(dst, 1, l));
      end;
      if c > 0 then
        ComboBox1.ItemIndex := 0;
    finally
      ComboBox1.Items.EndUpdate;
    end;

  end else
    LogOut(SFailedToLoadUPDILib);

  ports := GetSerialPortNames;
  ComboBox2.Items.Text := ports;
end;

procedure TForm1.FormDestroy(Sender: TObject);
begin
  if assigned(loclogger) then
  begin
    UPDILIB_logger_done(loclogger);
    loclogger := nil;
  end;
  if Assigned(cfg) then
  begin
    UPDILIB_cfg_done(cfg);
    cfg := nil;
  end;
  if IsUPDILibloaded then
    DestroyUPDILibInterface;
end;

procedure TForm1.Button1Click(Sender: TObject);
begin
  if IsUPDILibloaded then
  begin
    if Assigned(cfg) then
      UPDILIB_erase(cfg);
  end;
end;

procedure TForm1.ComboBox1Change(Sender: TObject);
var
  dev : AnsiString;
begin
  if IsUPDILibloaded then
  begin
    if Assigned(cfg) and (ComboBox1.ItemIndex >= 0) then
    begin
      dev := ComboBox1.Items[ComboBox1.ItemIndex];
      UPDILIB_cfg_set_device(cfg, PChar(dev));
    end;
  end;
end;

procedure TForm1.ComboBox1Select(Sender: TObject);
begin
  ComboBox1Change(Sender);
end;

procedure TForm1.ComboBox2Select(Sender: TObject);
begin

end;

procedure TForm1.LogOut(const Str: String);
begin
  Log.Lines.Add(Str);
end;

end.
