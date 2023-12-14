https://cloud.google.com/docs/get-started?refresh=1&hl=ko
https://cloud.google.com/speech-to-text/docs/before-you-begin?hl=ko
https://github.com/googleapis/google-cloud-cpp/tree/main/google/cloud/speech/quickstart

구글 STT 사용 시
1. 구글 클라우드 콘솔에서 프로젝트 생성 
2. Cloud Speech-to-Text API를 활성화
3. 서비스 계정 키(JSON 형식) 생성 후 다운로드
4. GOOGLE_APPLICATION_CREDENTIALS 환경 변수를 서비스 계정 키 경로로 설정
5. @powershell -NoProfile -ExecutionPolicy unrestricted -Command (new-object System.Net.WebClient).Downloadfile('https://pki.google.com/roots.pem', 'roots.pem') cmd에서 실행
6. GRPC_DEFAULT_SSL_ROOTS_FILE_PATH 환경 변수를 다운로드된 roots.pem 파일 경로로 설정

vcpkg.bat은 cmake 캐시 생성 과정에서 자동으로 실행되지만 처음 실행 시 20분 정도 시간이 걸리고 실행 로그가 visual studio 출력에 나오지 않으므로 따로 실행하는 것을 추천함.
