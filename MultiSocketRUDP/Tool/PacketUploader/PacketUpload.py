import yaml
import gspread
import json
import os

from google.oauth2.service_account import Credentials

BASE_PATH = os.path.dirname(os.path.abspath(__file__))

def load_config():
    config_path = os.path.join(BASE_PATH, 'config.json')
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"설정 파일을 찾을 수 없습니다: {config_path}")
        
    with open(config_path, 'r', encoding='utf-8') as f:
        return json.load(f)

def parse_yaml(file_path):
    with open(file_path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)
    
    rows = [["Type", "PacketName", "Description", "ItemType", "ItemName"]]
    
    for packet in data.get('Packet', []):
        p_type = packet.get('Type', '')
        p_name = packet.get('PacketName', '')
        p_desc = packet.get('Desc', '')
        
        items = packet.get('Items', [])
        if not items:
            rows.append([p_type, p_name, p_desc, "-", "-"])
        else:
            for item in items:
                rows.append([p_type, p_name, p_desc, item.get('Type', ''), item.get('Name', '')])
    
    return rows

def upload_to_sheets():
    config = load_config()
    
    auth_file_name = config.get('auth_file')
    auth_path = os.path.join(BASE_PATH, auth_file_name)

    if not os.path.exists(auth_path):
        raise FileNotFoundError(f"auth_file을 찾을 수 없습니다: {auth_path}")

    scopes = ["https://www.googleapis.com/auth/spreadsheets"]
    creds = Credentials.from_service_account_file(auth_path, scopes=scopes)
    client = gspread.authorize(creds)
    
    try:
        spreadsheet = client.open_by_key(config['spreadsheet_id'])
        worksheet = spreadsheet.worksheet(config['sheet_name'])
    except Exception as e:
        print(f"에러: 시트를 찾을 수 없습니다. ID와 공유 설정을 확인하세요. ({e})")
        return

    yaml_path = os.path.normpath(os.path.join(BASE_PATH, config['yaml_file']))
    data_rows = parse_yaml(yaml_path)
    
    worksheet.clear()
    worksheet.update('A1', data_rows)
    
    print(f"성공: {len(data_rows)-1}개의 패킷 데이터를 '{config['sheet_name']}'에 업로드했습니다.")

if __name__ == "__main__":
    upload_to_sheets()