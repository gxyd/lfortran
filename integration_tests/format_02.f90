      program format_02
      implicit none
      integer :: i6, format(3)
      i6 = 2
      format(i6) = 3
    1 format(i6)
      end program
